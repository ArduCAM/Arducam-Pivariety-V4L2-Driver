// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Arducam Pivariety Cameras
 * Copyright (C) 2021, Arducam
 * 
 * Based on Sony IMX219 camera driver
 * Copyright (C) 2019, Raspberry Pi (Trading) Ltd
 *
 * Based on Sony imx258 camera driver
 * Copyright (C) 2018 Intel Corporation
 *
 * DT / fwnode changes, and regulator / GPIO control taken from imx214 driver
 * Copyright 2018 Qtechnology A/S
 *
 * Flip handling taken from the Sony IMX319 driver.
 * Copyright (C) 2018 Intel Corporation
 *
 */
#include "arducam.h"
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <asm/unaligned.h>

#define arducam_REG_VALUE_08BIT		1
#define arducam_REG_VALUE_16BIT		2
#define arducam_REG_VALUE_32BIT		4

#define arducam_REG_MODE_SELECT		0x0100
#define arducam_MODE_STANDBY		0x00
#define arducam_MODE_STREAMING		0x01

/* V_TIMING internal */
#define arducam_REG_VTS			0x0160
#define arducam_VTS_15FPS		0x0dc6
#define arducam_VTS_30FPS_1080P		0x06e3
#define arducam_VTS_30FPS_BINNED		0x06e3
#define arducam_VTS_MAX			0xffff

/*Frame Length Line*/
#define arducam_FLL_MIN			0x08a6
#define arducam_FLL_MAX			0xffff
#define arducam_FLL_STEP			1
#define arducam_FLL_DEFAULT		0x0c98

/* HBLANK control - read only */
#define arducam_PPL_DEFAULT		5352

/* Exposure control */
#define arducam_REG_EXPOSURE		0x015a
#define arducam_EXPOSURE_MIN		4
#define arducam_EXPOSURE_STEP		1
#define arducam_EXPOSURE_DEFAULT		0x640
#define arducam_EXPOSURE_MAX		65535

/* Analog gain control */
#define arducam_REG_ANALOG_GAIN		0x0157
#define arducam_ANA_GAIN_MIN		0
#define arducam_ANA_GAIN_MAX		232
#define arducam_ANA_GAIN_STEP		1
#define arducam_ANA_GAIN_DEFAULT		0x0

/* Digital gain control */
#define arducam_REG_DIGITAL_GAIN		0x0158
#define arducam_DGTL_GAIN_MIN		0x0100
#define arducam_DGTL_GAIN_MAX		0x0fff
#define arducam_DGTL_GAIN_DEFAULT	0x0100
#define arducam_DGTL_GAIN_STEP		1

/* Test Pattern Control */
#define arducam_REG_TEST_PATTERN		0x0600
#define arducam_TEST_PATTERN_DISABLE	0
#define arducam_TEST_PATTERN_SOLID_COLOR	1
#define arducam_TEST_PATTERN_COLOR_BARS	2
#define arducam_TEST_PATTERN_GREY_COLOR	3
#define arducam_TEST_PATTERN_PN9		4

/* Embedded metadata stream structure */
#define ARDUCAM_EMBEDDED_LINE_WIDTH 16384
#define ARDUCAM_NUM_EMBEDDED_LINES 1

enum pad_types {
	IMAGE_PAD,
	METADATA_PAD,
	NUM_PADS
};

// #define VBLANK_TEST
static int debug = 0;
module_param(debug, int, 0644);

struct arducam_reg {
	u16 address;
	u8 val;
};

struct arducam_reg_list {
	u32 num_of_regs;
	const struct arducam_reg *regs;
};

/* Mode : resolution and related config&values */
struct arducam_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;
	/* V-timing */
	u32 vts_def;
	/* Default register values */
	struct arducam_reg_list reg_list;
};
static const struct arducam_reg mode_1920_1080_regs[] = {
};


static const char * const arducam_effect_menu[] = {
	"Normal",
	"Alien",
	"Antique",
	"Black/White",
	"Emboss",
	"Emboss/Color",
	"Grayscale",
	"Negative",
	"Blueish",
	"Greenish",
	"Redish",
	"Posterize 1",
	"Posterize 2",
	"Sepia 1",
	"Sepia 2",
	"Sketch",
	"Solarize",
	"Foggy",
};

static const char * const arducam_pan_menu[] = {
	"Center",
	"Top Left",
	"Top Right",
	"Bottom Left",
	"Bottom Right",
};
static const char * const arducam_zoom_menu[] = {
	"1X",
	"2X",
	"3X",
	"4X",
};
static const char * const arducam_pan_zoom_speed_menu[] = {
	"Immediate",
	"slow",
	"fast",
};

static const char * const arducam_denoise_menu[] = {
	"denoise = -8",
	"denoise = -4",
	"denoise = -2",
	"denoise = -1",
	"denoise = -0.5",
	"denoise = 0",
	"denoise = 0.5",
	"denoise = 1",
	"denoise = 2",
	"denoise = 4",
	"denoise = 8",
};

static const char * const arducam_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"PN9"
};

static const int arducam_test_pattern_val[] = {
	arducam_TEST_PATTERN_DISABLE,
	arducam_TEST_PATTERN_COLOR_BARS,
	arducam_TEST_PATTERN_SOLID_COLOR,
	arducam_TEST_PATTERN_GREY_COLOR,
	arducam_TEST_PATTERN_PN9,
};

/* regulator supplies */
static const char * const arducam_supply_name[] = {
	/* Supplies can be enabled in any order */
	"VANA",  /* Analog (2.8V) supply */
	"VDIG",  /* Digital Core (1.8V) supply */
	"VDDL",  /* IF (1.2V) supply */
};

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 codes[] = {
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_Y8_1X8,

	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_Y10_1X10,

	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_Y12_1X12,
};

#define arducam_NUM_SUPPLIES ARRAY_SIZE(arducam_supply_name)

#define arducam_XCLR_DELAY_MS 10	/* Initialisation delay after XCLR low->high */
#define arducam_XCLR_MIN_DELAY_US	6200
#define arducam_XCLR_DELAY_RANGE_US	1000
/* Mode configs */
static const struct arducam_mode supported_modes[] = {
	{
		/* 1080P 30fps cropped */
		.width = 1920,
		.height = 1080,
		.vts_def = arducam_VTS_30FPS_1080P,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1920_1080_regs),
			.regs = mode_1920_1080_regs,
		},
	},
};

struct arducam {
	struct v4l2_subdev sd;
	struct media_pad pad[NUM_PADS];

	struct v4l2_fwnode_endpoint ep; /* the parsed DT endpoint info */
	struct clk *xclk; /* system clock to arducam */
	u32 xclk_freq;
	struct gpio_desc *reset_gpio;
    struct i2c_client *client;
	struct arducam_format *supported_formats;
	int num_supported_formats;
	int current_format_idx;
	int current_resolution_idx;
	int lanes;
	struct gpio_desc *xclr_gpio;
	struct regulator_bulk_data supplies[arducam_NUM_SUPPLIES];

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;

	/* Current mode */
	const struct arducam_mode *mode;
	int bayer_order_volatile;
	struct v4l2_rect crop;
	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;

	int power_count;
	/* Streaming on/off */
	bool streaming;
	bool wait_until_free;
	struct v4l2_ctrl *ctrls[32];
};

static int is_raw(int pixformat);
static u32 data_type_to_mbus_code(int data_type, int bayer_order);


static inline struct arducam *to_arducam(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct arducam, sd);
}

/* Write registers up to 2 at a time */
static int arducam_write_reg(struct arducam *arducam, u16 reg, u32 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	u8 buf[6];

	v4l2_dbg(1, debug, client, "%s: Write 0x%04x to register 0x%02x.\n",
			 __func__, val, reg);

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int arducam_readl_reg(struct i2c_client *client,
								   u16 addr, u32 *val)
{
    u16 buf = htons(addr);
    u32 data;
    struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags= 0,
			.len = 2,
			.buf = (u8 *)&buf,
		},
		{
			.addr = client->addr,
			.flags= I2C_M_RD,
			.len = 4,
			.buf = (u8 *)&data,
		},
	};

	if(i2c_transfer(client->adapter, msgs, 2) != 2){
		return -1;
	}

	*val = ntohl(data);

	return 0;
}

static int arducam_writel_reg(struct i2c_client *client,
									u16 addr, u32 val)
{
	u8 data[6];
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags= 0,
			.len = 6,
			.buf = data,
		},
	};
	addr = htons(addr);
	val = htonl(val);
	memcpy(data, &addr, 2);
	memcpy(data + 2, &val, 4);

	if(i2c_transfer(client->adapter, msgs, 1) != 1)
		return -1;

	return 0;
}

int arducam_read(struct i2c_client *client, u16 addr, u32 *value)
{
	int ret;
	int count = 0;
	while (count++ < I2C_READ_RETRY_COUNT) {
		ret = arducam_readl_reg(client, addr, value);
		if(!ret) {
			v4l2_dbg(1, debug, client, "%s: 0x%02x 0x%04x\n",
				__func__, addr, *value);
			return ret;
		}
	}
	
	v4l2_err(client, "%s: Reading register 0x%02x failed\n",
			 __func__, addr);
	return ret;
}

static int wait_for_free(struct i2c_client *client, int interval) {
	u32 value;
	u32 count = 0;
	while(count++ < (1000 / interval)) {
		int ret = arducam_read(client, SYSTEM_IDLE_REG, &value);
		if (!ret && !value) break;
		msleep(interval);
	}
	v4l2_dbg(1, debug, client, "%s: End wait, Count: %d.\n",
			 __func__, count);

	return 0;
}

int arducam_write(struct i2c_client *client, u16 addr, u32 value)
{
	int ret;
	int count = 0;
	while (count++ < I2C_WRITE_RETRY_COUNT) {
		ret = arducam_writel_reg(client, addr, value);
		if(!ret)
			return ret;
	}
	v4l2_err(client, "%s: Write 0x%04x to register 0x%02x failed\n",
			 __func__, value, addr);
	return ret;
}

/* Get bayer order based on flip setting. */
static u32 arducam_get_format_code(struct arducam *priv, struct arducam_format *format)
{
	unsigned int i, index;

	if (!priv->bayer_order_volatile)
		return data_type_to_mbus_code(format->data_type, format->bayer_order);

	lockdep_assert_held(&priv->mutex);

	i = format->bayer_order;

	index = i;

	i = (priv->hflip->val ? i^1 : i);
	i = (priv->vflip->val ? i^2 : i);

	v4l2_dbg(1, debug, priv->client, "%s: before: %d, after: %d.\n",
			 __func__, index, i);

	return data_type_to_mbus_code(format->data_type, i);
}

/* Power/clock management functions */
static int arducam_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);
	int ret;

	ret = regulator_bulk_enable(arducam_NUM_SUPPLIES,
				    arducam->supplies);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable regulators\n",
			__func__);
		return ret;
	}

	ret = clk_prepare_enable(arducam->xclk);
	if (ret) {
		dev_err(&client->dev, "%s: failed to enable clock\n",
			__func__);
		goto reg_off;
	}

	gpiod_set_value_cansleep(arducam->reset_gpio, 1);
	usleep_range(arducam_XCLR_MIN_DELAY_US,
		     arducam_XCLR_MIN_DELAY_US + arducam_XCLR_DELAY_RANGE_US);

	return 0;

reg_off:
	regulator_bulk_disable(arducam_NUM_SUPPLIES, arducam->supplies);

	return ret;
}

static int arducam_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);

	gpiod_set_value_cansleep(arducam->reset_gpio, 0);
	regulator_bulk_disable(arducam_NUM_SUPPLIES, arducam->supplies);
	clk_disable_unprepare(arducam->xclk);

	return 0;
}

static int arducam_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct arducam *arducam = to_arducam(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, IMAGE_PAD);

	struct v4l2_mbus_framefmt *try_fmt_meta =
		v4l2_subdev_get_try_format(sd, fh->pad, METADATA_PAD);

	/* Initialize try_fmt */
	try_fmt->width = arducam->supported_formats[0].resolution_set->width;
	try_fmt->height = arducam->supported_formats[0].resolution_set->height;
	try_fmt->code = arducam->supported_formats[0].mbus_code;
	try_fmt->field = V4L2_FIELD_NONE;

	/* Initialize try_fmt for the embedded metadata pad */
	try_fmt_meta->width = ARDUCAM_EMBEDDED_LINE_WIDTH;
	try_fmt_meta->height = ARDUCAM_NUM_EMBEDDED_LINES;
	try_fmt_meta->code = MEDIA_BUS_FMT_SENSOR_DATA;
	try_fmt_meta->field = V4L2_FIELD_NONE;

	return 0;
}

static int arducam_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret, i;
	struct arducam *priv = 
		container_of(ctrl->handler, struct arducam, ctrl_handler);
	struct arducam_format *supported_formats = priv->supported_formats;
	int num_supported_formats = priv->num_supported_formats;

	if (ctrl->id == V4L2_CID_VFLIP || ctrl->id == V4L2_CID_HFLIP) {
		for (i = 0; i < num_supported_formats; i++) {
			supported_formats[i].mbus_code = 
				arducam_get_format_code(
					priv, &supported_formats[i]);
		}
	}

	v4l2_dbg(1, debug, priv->client, "%s: cid = (0x%X), value = (%d).\n",
			 __func__, ctrl->id, ctrl->val);
	

	ret = arducam_write(priv->client, CTRL_ID_REG, ctrl->id);
	ret += arducam_write(priv->client, CTRL_VALUE_REG, ctrl->val);
	if (ret < 0)
		return -EINVAL;

	// When starting streaming, controls are set in batches, 
	// and the short interval will cause some controls to be unsuccessfully set.
	if (priv->wait_until_free)
		wait_for_free(priv->client, 1);
	else
		usleep_range(200, 210);

	return 0;
}


static const struct v4l2_ctrl_ops arducam_ctrl_ops = {
	.s_ctrl = arducam_s_ctrl,
};

static int arducam_csi2_enum_mbus_code(
			struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	struct arducam *priv = to_arducam(sd);
	struct arducam_format *supported_formats = priv->supported_formats;
	int num_supported_formats = priv->num_supported_formats;
	
	if (code->pad >= NUM_PADS)
		return -EINVAL;

	v4l2_dbg(1, debug, sd, "%s: index = (%d)\n", __func__, code->index);
	
	if (code->pad == IMAGE_PAD) {
		if (code->index >= num_supported_formats)
			return -EINVAL;
		code->code = supported_formats[code->index].mbus_code;
	} else {
		if (code->index > 0)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_SENSOR_DATA;
	}

	return 0;
}

static int arducam_csi2_enum_framesizes(
			struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_size_enum *fse)
{
	int i;
	struct arducam *priv = to_arducam(sd);
	struct arducam_format *supported_formats = priv->supported_formats;
	int num_supported_formats = priv->num_supported_formats;

	if (fse->pad >= NUM_PADS)
		return -EINVAL;

	v4l2_dbg(1, debug, sd, "%s: code = (0x%X), index = (%d)\n",
			 __func__, fse->code, fse->index);

	if (fse->pad == IMAGE_PAD) {
		for (i = 0; i < num_supported_formats; i++) {
			if (fse->code == supported_formats[i].mbus_code) {
				if (fse->index >= supported_formats[i].num_resolution_set)
					return -EINVAL;
				fse->min_width = fse->max_width =
					supported_formats[i].resolution_set[fse->index].width;
				fse->min_height = fse->max_height =
					supported_formats[i].resolution_set[fse->index].height;
				return 0;
			}
		}
	} else {
		if (fse->code != MEDIA_BUS_FMT_SENSOR_DATA || fse->index > 0)
			return -EINVAL;

		fse->min_width = ARDUCAM_EMBEDDED_LINE_WIDTH;
		fse->max_width = fse->min_width;
		fse->min_height = ARDUCAM_NUM_EMBEDDED_LINES;
		fse->max_height = fse->min_height;
	}

	return -EINVAL;
}

static void arducam_update_metadata_pad_format(struct v4l2_subdev_format *fmt)
{
	fmt->format.width = ARDUCAM_EMBEDDED_LINE_WIDTH;
	fmt->format.height = ARDUCAM_NUM_EMBEDDED_LINES;
	fmt->format.code = MEDIA_BUS_FMT_SENSOR_DATA;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int arducam_csi2_get_fmt(struct v4l2_subdev *sd,
								struct v4l2_subdev_pad_config *cfg,
								struct v4l2_subdev_format *format)
{
	struct arducam *priv = to_arducam(sd);
	struct arducam_format *current_format;
	
	if (format->pad >= NUM_PADS)
		return -EINVAL;

	mutex_lock(&priv->mutex);

	if (format->pad == IMAGE_PAD) {
		current_format = &priv->supported_formats[priv->current_format_idx];
		format->format.width =
			current_format->resolution_set[priv->current_resolution_idx].width;
		format->format.height =
			current_format->resolution_set[priv->current_resolution_idx].height;
		format->format.code = current_format->mbus_code;
		format->format.field = V4L2_FIELD_NONE;
		format->format.colorspace = V4L2_COLORSPACE_SRGB;

		v4l2_dbg(1, debug, sd, "%s: width: (%d) height: (%d) code: (0x%X)\n",
			__func__, format->format.width,format->format.height,
				format->format.code);
	} else {
		arducam_update_metadata_pad_format(format);
	}

	mutex_unlock(&priv->mutex);
	return 0;
}

static int arducam_csi2_get_fmt_idx_by_code(struct arducam *priv,
											u32 mbus_code)
{
	int i;
	u32 data_type;
	struct arducam_format *formats = priv->supported_formats;

	for (i = 0; i < ARRAY_SIZE(codes); i++)
		if (codes[i] == mbus_code)
			break;

	if (i / 5 < 3)
		data_type = i / 5 + 0x2a;
	else 
		data_type = U32_MAX;

	for (i = 0; i < priv->num_supported_formats; i++) {
		if (formats[i].mbus_code == mbus_code)
			return i; 
	}

	if (data_type != U32_MAX) {
		for (i = 0; i < priv->num_supported_formats; i++) {
			if (formats[i].data_type == data_type)
				return i; 
		}
	}

	return -EINVAL;
}

static struct v4l2_ctrl *get_control(struct arducam *priv, u32 id) {
	int index = 0;
	while (priv->ctrls[index]) {
		if (priv->ctrls[index]->id == id)
			return priv->ctrls[index];
		index++;
	}
	return NULL;
}

static int update_control(struct arducam *priv, u32 id)
{
	int ret = 0;
	struct i2c_client *client = priv->client;
	struct v4l2_ctrl *ctrl;
	u32 min, max, step, def, id2;

	arducam_write(client, CTRL_ID_REG, id);
	arducam_read(client, CTRL_ID_REG, &id2);
	v4l2_dbg(1, debug, priv->client, "%s: Write ID: 0x%08X Read ID: 0x%08X\n",
		__func__, id, id2);
	arducam_write(client, CTRL_VALUE_REG, 0);
	wait_for_free(client, 1);
	ret += arducam_read(client, CTRL_MAX_REG, &max);
	ret += arducam_read(client, CTRL_MIN_REG, &min);
	ret += arducam_read(client, CTRL_DEF_REG, &def);
	ret += arducam_read(client, CTRL_STEP_REG, &step);
	if (ret < 0)
		goto err;
	if (id == NO_DATA_AVAILABLE || max == NO_DATA_AVAILABLE ||
		min == NO_DATA_AVAILABLE || def == NO_DATA_AVAILABLE ||
		step == NO_DATA_AVAILABLE)
		goto err;

	v4l2_dbg(1, debug, priv->client,
		 "%s: min: %d, max: %d, step: %d, def: %d\n",
		 __func__, min, max, step, def);
	ctrl = get_control(priv, id);
	__v4l2_ctrl_modify_range(ctrl, min, max, step, def);

err:
	return -EINVAL;
}

static int update_controls(struct arducam *priv) {
	int ret = 0;
	wait_for_free(priv->client, 5);

	ret += update_control(priv, V4L2_CID_ARDUCAM_FRAME_RATE);
	ret += update_control(priv, V4L2_CID_HBLANK);
	ret += update_control(priv, V4L2_CID_VBLANK);
	ret += update_control(priv, V4L2_CID_PIXEL_RATE);

	return ret;
}


static int arducam_csi2_set_fmt(struct v4l2_subdev *sd,
								struct v4l2_subdev_pad_config *cfg,
								struct v4l2_subdev_format *format)
{
	int i, j;
	struct arducam *priv = to_arducam(sd);
	struct arducam_format *supported_formats = priv->supported_formats;

	if (format->pad >= NUM_PADS)
		return -EINVAL;

	if (format->pad == IMAGE_PAD) {
		format->format.colorspace = V4L2_COLORSPACE_SRGB;
		format->format.field = V4L2_FIELD_NONE;

		v4l2_dbg(1, debug, sd, "%s: code: 0x%X, width: %d, height: %d\n",
				__func__, format->format.code, format->format.width,
					format->format.height);

		i = arducam_csi2_get_fmt_idx_by_code(priv, format->format.code);
		if (i < 0)
			return -EINVAL;

		format->format.code = supported_formats[i].mbus_code;
		// format->format.code = arducam_get_format_code(priv, format->format.code);

		for (j = 0; j < supported_formats[i].num_resolution_set; j++) {
			if (supported_formats[i].resolution_set[j].width 
					== format->format.width && 
				supported_formats[i].resolution_set[j].height
					== format->format.height) {

				v4l2_dbg(1, debug, sd, "%s: format match.\n", __func__);
				v4l2_dbg(1, debug, sd, "%s: set format to device: %d %d.\n",
					__func__, supported_formats[i].index, j);

				arducam_write(priv->client, PIXFORMAT_INDEX_REG,
					supported_formats[i].index);
				arducam_write(priv->client, RESOLUTION_INDEX_REG, j);
				
				priv->current_format_idx = i;
				priv->current_resolution_idx = j;

				update_controls(priv);
				return 0;
			}
		}
		format->format.width = supported_formats[i].resolution_set[0].width;
		format->format.height = supported_formats[i].resolution_set[0].height;

		arducam_write(priv->client, PIXFORMAT_INDEX_REG,
			supported_formats[i].index);
		arducam_write(priv->client, RESOLUTION_INDEX_REG, 0);

		priv->current_format_idx = i;
		priv->current_resolution_idx = 0;
		update_controls(priv);
	} else {
		arducam_update_metadata_pad_format(format);
	}


	return 0;
}

/* Start streaming */
static int arducam_start_streaming(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	int ret;

	/* set stream on register */
	ret =  arducam_write_reg(arducam, arducam_REG_MODE_SELECT,
				arducam_REG_VALUE_32BIT, arducam_MODE_STREAMING);

	if (ret)
		return ret;

	wait_for_free(client, 2);

	arducam->wait_until_free = true;
	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(arducam->sd.ctrl_handler);

	arducam->wait_until_free = false;
	if (ret)
		return ret;

	wait_for_free(client, 2);

	return ret;
}

static int arducam_read_sel(struct arducam *arducam, struct v4l2_rect *rect) {
	struct i2c_client *client = arducam->client;
	int ret = 0;
	ret += arducam_read(client, IPC_SEL_TOP_REG, &rect->top);
	ret += arducam_read(client, IPC_SEL_LEFT_REG, &rect->left);
	ret += arducam_read(client, IPC_SEL_WIDTH_REG, &rect->width);
	ret += arducam_read(client, IPC_SEL_HEIGHT_REG, &rect->height);

	if (ret || rect->top == NO_DATA_AVAILABLE 
		|| rect->left == NO_DATA_AVAILABLE 
		|| rect->width == NO_DATA_AVAILABLE 
		|| rect->height == NO_DATA_AVAILABLE) {
			v4l2_err(client, "%s: Failed to read selection.\n",
			 	 __func__);
			return -EINVAL;
		}
	return 0;
}

static const struct v4l2_rect *
__arducam_get_pad_crop(struct arducam *arducam, struct v4l2_subdev_pad_config *cfg,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	int ret;
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&arducam->sd, cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		ret = arducam_read_sel(arducam, &arducam->crop);
		if (ret)
			return NULL;
		return &arducam->crop;
	}

	return NULL;
}

static int arducam_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	int ret = 0;
	struct v4l2_rect rect;
	struct arducam *arducam = to_arducam(sd);
	struct i2c_client *client = arducam->client;
	
	ret = arducam_write(client, IPC_SEL_TARGET_REG, sel->target);
	if (ret) {
		v4l2_err(client, "%s: Write register 0x%02x failed\n",
			 	 __func__, IPC_SEL_TARGET_REG);
		return -EINVAL;
	}

	wait_for_free(client, 2);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP: {

		mutex_lock(&arducam->mutex);
		sel->r = *__arducam_get_pad_crop(arducam, cfg, sel->pad,
						sel->which);
		mutex_unlock(&arducam->mutex);

		return 0;
	}

	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		ret = arducam_read_sel(arducam, &rect);
		if (ret) {
			return -EINVAL;
		}
		sel->r = rect;
		return 0;
	}

	return -EINVAL;
}

/* Stop streaming */
static int arducam_stop_streaming(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	int ret;

	/* set stream off register */
	ret = arducam_write_reg(arducam, arducam_REG_MODE_SELECT,
			       arducam_REG_VALUE_32BIT, arducam_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "%s failed to set stream\n", __func__);

	/*
	 * Return success even if it was an error, as there is nothing the
	 * caller can do about it.
	 */
	return 0;
}

static int arducam_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct arducam *arducam = to_arducam(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&arducam->mutex);
	if (arducam->streaming == enable) {
		mutex_unlock(&arducam->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto err_unlock;
		}

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = arducam_start_streaming(arducam);
		if (ret)
			goto err_rpm_put;
	} else {
		arducam_stop_streaming(arducam);
		pm_runtime_put(&client->dev);
	}

	arducam->streaming = enable;

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(arducam->vflip, enable);
	__v4l2_ctrl_grab(arducam->hflip, enable);

	mutex_unlock(&arducam->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&arducam->mutex);

	return ret;
}

static int __maybe_unused arducam_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);

	if (arducam->streaming)
		arducam_stop_streaming(arducam);

	return 0;
}

static int __maybe_unused arducam_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);
	int ret;

	if (arducam->streaming) {
		ret = arducam_start_streaming(arducam);
		if (ret)
			goto error;
	}

	return 0;

error:
	arducam_stop_streaming(arducam);
	arducam->streaming = 0;
	return ret;
}

static int arducam_get_regulators(struct arducam *arducam)
{
	struct i2c_client *client = v4l2_get_subdevdata(&arducam->sd);
	int i;

	for (i = 0; i < arducam_NUM_SUPPLIES; i++)
		arducam->supplies[i].supply = arducam_supply_name[i];

	return devm_regulator_bulk_get(&client->dev,
				       arducam_NUM_SUPPLIES,
				       arducam->supplies);
}

static const struct v4l2_subdev_core_ops arducam_core_ops = {
	// .s_power = arducam_s_power,
};

static const struct v4l2_subdev_video_ops arducam_video_ops = {
	.s_stream = arducam_set_stream,
};

static const struct v4l2_subdev_pad_ops arducam_pad_ops = {
	.enum_mbus_code = arducam_csi2_enum_mbus_code,
	.get_fmt = arducam_csi2_get_fmt,
	.set_fmt = arducam_csi2_set_fmt,
	.enum_frame_size = arducam_csi2_enum_framesizes,
	.get_selection = arducam_get_selection,
};

static const struct v4l2_subdev_ops arducam_subdev_ops = {
	.core = &arducam_core_ops,
	.video = &arducam_video_ops,
	.pad = &arducam_pad_ops,
};

static const struct v4l2_subdev_internal_ops arducam_internal_ops = {
	.open = arducam_open,
};

static void arducam_free_controls(struct arducam *arducam)
{
	v4l2_ctrl_handler_free(arducam->sd.ctrl_handler);
	mutex_destroy(&arducam->mutex);
}

static int arducam_get_length_of_set(struct i2c_client *client,
									u16 idx_reg, u16 val_reg)
{
	int ret;
	int index = 0;
	u32 val;
	while (1) {
		ret = arducam_write(client, idx_reg, index);
		ret += arducam_read(client, val_reg, &val);

		if (ret < 0)
			return -1;

		if (val == NO_DATA_AVAILABLE)
			break;
		index++;
	}
	arducam_write(client, idx_reg, 0);
	return index;
}
static int is_raw(int pixformat)
{
	return pixformat >= 0x28 && pixformat <= 0x2D;
}
static u32 bayer_to_mbus_code(int data_type, int bayer_order)
{
	const uint32_t depth8[] = {
        MEDIA_BUS_FMT_SBGGR8_1X8,
		MEDIA_BUS_FMT_SGBRG8_1X8,
        MEDIA_BUS_FMT_SGRBG8_1X8,
		MEDIA_BUS_FMT_SRGGB8_1X8,
		MEDIA_BUS_FMT_Y8_1X8,
	};
    const uint32_t depth10[] = {
        MEDIA_BUS_FMT_SBGGR10_1X10,
		MEDIA_BUS_FMT_SGBRG10_1X10,
        MEDIA_BUS_FMT_SGRBG10_1X10,
		MEDIA_BUS_FMT_SRGGB10_1X10,
		MEDIA_BUS_FMT_Y10_1X10,
	};
    const uint32_t depth12[] = {
        MEDIA_BUS_FMT_SBGGR12_1X12,
		MEDIA_BUS_FMT_SGBRG12_1X12,
        MEDIA_BUS_FMT_SGRBG12_1X12,
		MEDIA_BUS_FMT_SRGGB12_1X12,
		MEDIA_BUS_FMT_Y12_1X12,
	};
    // const uint32_t depth16[] = {
	// 	MEDIA_BUS_FMT_SBGGR16_1X16,
	// 	MEDIA_BUS_FMT_SGBRG16_1X16,
    //     MEDIA_BUS_FMT_SGRBG16_1X16,
	// 	MEDIA_BUS_FMT_SRGGB16_1X16,
    // };
    if (bayer_order < 0 || bayer_order > 4) {
        return 0;
    }

    switch (data_type) {
    case IMAGE_DT_RAW8:
        return depth8[bayer_order];
    case IMAGE_DT_RAW10:
        return depth10[bayer_order];
    case IMAGE_DT_RAW12:
        return depth12[bayer_order];
    }
    return 0;
}
static u32 yuv422_to_mbus_code(int data_type, int order)
{
	const uint32_t depth8[] = {
        MEDIA_BUS_FMT_YUYV8_1X16,
		MEDIA_BUS_FMT_YVYU8_1X16,
        MEDIA_BUS_FMT_UYVY8_1X16,
		MEDIA_BUS_FMT_VYUY8_1X16,
	};

	const uint32_t depth10[] = {
        MEDIA_BUS_FMT_YUYV10_1X20,
		MEDIA_BUS_FMT_YVYU10_1X20,
		MEDIA_BUS_FMT_UYVY10_1X20,
		MEDIA_BUS_FMT_VYUY10_1X20,
	};

	if (order < 0 || order > 3) {
        return 0;
    }

	switch(data_type) {
	case IMAGE_DT_YUV422_8:
		return depth8[order];
	case IMAGE_DT_YUV422_10:
		return depth10[order];
	}
    return 0;
}

static u32 data_type_to_mbus_code(int data_type, int bayer_order)
{
    if(is_raw(data_type)) {
		return bayer_to_mbus_code(data_type, bayer_order);
	}

	switch(data_type) {
	case IMAGE_DT_YUV422_8:
	case IMAGE_DT_YUV422_10:
		return yuv422_to_mbus_code(data_type, bayer_order);
	case IMAGE_DT_RGB565:
		return MEDIA_BUS_FMT_RGB565_2X8_LE;//MEDIA_BUS_FMT_RGB565_1X16;
	case IMAGE_DT_RGB888:
		return MEDIA_BUS_FMT_RGB888_1X24;//MEDIA_BUS_FMT_RGB565_1X16;
	}
	return 0;
}
static int arducam_enum_resolution(struct i2c_client *client,
								struct arducam_format *format)
{
	int index = 0;
	u32 width, height;
	int num_resolution = 0;
	int ret;
	
	num_resolution = arducam_get_length_of_set(client,
						RESOLUTION_INDEX_REG, FORMAT_WIDTH_REG);
	if (num_resolution < 0)
		goto err;

	format->resolution_set = devm_kzalloc(&client->dev,
			sizeof(*(format->resolution_set)) * num_resolution, GFP_KERNEL);
	while (1) {
		ret = arducam_write(client, RESOLUTION_INDEX_REG, index);
		ret += arducam_read(client, FORMAT_WIDTH_REG, &width);
		ret += arducam_read(client, FORMAT_HEIGHT_REG, &height);

		if (ret < 0)
			goto err;

		if (width == NO_DATA_AVAILABLE || height == NO_DATA_AVAILABLE)
			break;

		format->resolution_set[index].width = width;
		format->resolution_set[index].height= height;

		index++;
	}
	format->num_resolution_set = index;
    arducam_write(client, RESOLUTION_INDEX_REG, 0);
	return 0;
err:
	return -ENODEV;
}

static int arducam_add_extension_pixformat(struct arducam *priv)
{
	int i;
	struct arducam_format *formats = priv->supported_formats;
	for (i = 0; i < priv->num_supported_formats; i++) {
		switch (formats[i].mbus_code){
		case MEDIA_BUS_FMT_SBGGR10_1X10:
		case MEDIA_BUS_FMT_SGBRG10_1X10:
        case MEDIA_BUS_FMT_SGRBG10_1X10:
		case MEDIA_BUS_FMT_SRGGB10_1X10:
		case MEDIA_BUS_FMT_Y10_1X10:
			formats[priv->num_supported_formats] = formats[i];
			formats[priv->num_supported_formats].mbus_code = 
				MEDIA_BUS_FMT_ARDUCAM_Y102Y16_1x16;
			priv->num_supported_formats++;
			return 0;
        case MEDIA_BUS_FMT_SBGGR12_1X12:
		case MEDIA_BUS_FMT_SGBRG12_1X12:
        case MEDIA_BUS_FMT_SGRBG12_1X12:
		case MEDIA_BUS_FMT_SRGGB12_1X12:
		case MEDIA_BUS_FMT_Y12_1X12:
			formats[priv->num_supported_formats] = formats[i];
			formats[priv->num_supported_formats].mbus_code = 
				MEDIA_BUS_FMT_ARDUCAM_Y122Y16_1x16;
			priv->num_supported_formats++;
			return 0;
		}
	}
	return -1;
}


static int arducam_enum_pixformat(struct arducam *priv)
{
	int ret = 0;
	u32 mbus_code = 0;
	int pixformat_type;
	int bayer_order;
	int bayer_order_not_volatile;
	int lanes;
	int index = 0;
	int num_pixformat = 0;
	struct i2c_client *client = priv->client;

	num_pixformat = arducam_get_length_of_set(client,
						PIXFORMAT_INDEX_REG, PIXFORMAT_TYPE_REG);

	if (num_pixformat < 0)
		goto err;

	ret = arducam_read(client, FLIPS_DONT_CHANGE_ORDER_REG, &bayer_order_not_volatile);
	if (bayer_order_not_volatile == NO_DATA_AVAILABLE)
		priv->bayer_order_volatile = 1;
	else
		priv->bayer_order_volatile = !bayer_order_not_volatile;

	if (ret < 0)
		goto err;

	priv->supported_formats = devm_kzalloc(&client->dev,
		sizeof(*(priv->supported_formats)) * (num_pixformat + 1), GFP_KERNEL);

	while (1) {
		ret = arducam_write(client, PIXFORMAT_INDEX_REG, index);
		ret += arducam_read(client, PIXFORMAT_TYPE_REG, &pixformat_type);

		if (pixformat_type == NO_DATA_AVAILABLE)
			break;

		ret += arducam_read(client, MIPI_LANES_REG, &lanes);
		if (lanes == NO_DATA_AVAILABLE)
			break;

		ret += arducam_read(client, PIXFORMAT_ORDER_REG, &bayer_order);
		if (ret < 0)
			goto err;

		mbus_code = data_type_to_mbus_code(pixformat_type, bayer_order);
		priv->supported_formats[index].index = index;
		priv->supported_formats[index].mbus_code = mbus_code;
		priv->supported_formats[index].bayer_order = bayer_order;
		priv->supported_formats[index].data_type = pixformat_type;
		if (arducam_enum_resolution(client,
				&priv->supported_formats[index]))
			goto err;

		index++;
	}
	arducam_write(client, PIXFORMAT_INDEX_REG, 0);
	priv->num_supported_formats = index;
	priv->current_format_idx = 0;
	priv->current_resolution_idx = 0;
	priv->lanes = lanes;
	// arducam_add_extension_pixformat(priv);
	return 0;

err:
	return -ENODEV;
}
static const char *arducam_ctrl_get_name(u32 id) {
	switch(id) {
	case V4L2_CID_ARDUCAM_EXT_TRI:
		return "trigger_mode";
	case V4L2_CID_ARDUCAM_FACE_DETECTION:
		return "face_detection";
	case V4L2_CID_EXPOSURE_AUTO:
		return "exposure_auto";
	case V4L2_CID_ARDUCAM_IRCUT:
		return "ircut";
	case V4L2_CID_ARDUCAM_FRAME_RATE:
		return "frame_rate";
	case V4L2_CID_ARDUCAM_EFFECTS:
		return "effects";
	case V4L2_CID_PAN_ABSOLUTE:
		return "pan";
	case V4L2_CID_ZOOM_ABSOLUTE:
		return "zoom";
	case V4L2_CID_ARDUCAM_PAN_X_ABSOLUTE:
		return "Pan Horizontal";
	case V4L2_CID_ARDUCAM_PAN_Y_ABSOLUTE:
		return "Pan Vertical";
	case V4L2_CID_ARDUCAM_ZOOM_PAN_SPEED:
		return "pan_zoom_speed";
	case V4L2_CID_ARDUCAM_HDR:
		return "hdr";
	case V4L2_CID_ARDUCAM_DENOISE:
		return "denoise";
	default:
		return NULL;
	}
}

enum v4l2_ctrl_type arducam_get_v4l2_ctrl_type(u32 id) {
	switch(id) {
	case V4L2_CID_ARDUCAM_EXT_TRI:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_FACE_DETECTION:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_EXPOSURE_AUTO:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_IRCUT:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_HDR:
		return V4L2_CTRL_TYPE_BOOLEAN;
	case V4L2_CID_ARDUCAM_FRAME_RATE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_EFFECTS:
		return V4L2_CTRL_TYPE_MENU;
	case V4L2_CID_PAN_ABSOLUTE:
		return V4L2_CTRL_TYPE_MENU;
	case V4L2_CID_ZOOM_ABSOLUTE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_PAN_X_ABSOLUTE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_PAN_Y_ABSOLUTE:
		return V4L2_CTRL_TYPE_INTEGER;
	case V4L2_CID_ARDUCAM_ZOOM_PAN_SPEED:
		return V4L2_CTRL_TYPE_MENU;
	case V4L2_CID_ARDUCAM_DENOISE:
		return V4L2_CTRL_TYPE_MENU;
	default:
		return V4L2_CTRL_TYPE_INTEGER;
	}
}

const char * const* arducam_get_v4l2_ctrl_menu(u32 id) {
	switch(id) {
	case V4L2_CID_ARDUCAM_EFFECTS:
		return arducam_effect_menu;
	case V4L2_CID_PAN_ABSOLUTE:
		return arducam_pan_menu;
	case V4L2_CID_ARDUCAM_ZOOM_PAN_SPEED:
		return arducam_pan_zoom_speed_menu;
	case V4L2_CID_ARDUCAM_DENOISE:
		return arducam_denoise_menu;
	default:
		return NULL;
	}
}

static struct v4l2_ctrl *v4l2_ctrl_new_arducam(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s64 min, s64 max, u64 step, s64 def)
{
	struct v4l2_ctrl_config cfg = {
		.ops = ops,
		.id = id,
		.name = NULL,
		.type = V4L2_CTRL_TYPE_BOOLEAN,//V4L2_CTRL_TYPE_INTEGER,
		.flags = 0,
		.min = min,
		.max = max,
		.def = def,
		.step = step,
	};
	cfg.name = arducam_ctrl_get_name(id);
	cfg.type = arducam_get_v4l2_ctrl_type(id);
	cfg.qmenu = arducam_get_v4l2_ctrl_menu(id);
	return v4l2_ctrl_new_custom(hdl, &cfg, NULL);
}

static int arducam_enum_controls(struct arducam *priv)
{
	int ret;
	int index = 0;
	int num_ctrls = 0;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	struct v4l2_fwnode_device_properties props;
	u32 id, min, max, def, step;
	struct i2c_client *client;
	ctrl_hdlr = &priv->ctrl_handler;
	client = priv->client;
	num_ctrls = arducam_get_length_of_set(client,
					CTRL_INDEX_REG, CTRL_ID_REG);
	if (num_ctrls < 0)
		goto err;
    v4l2_dbg(1, debug, priv->client, "%s: num_ctrls = %d\n",
				__func__,num_ctrls);
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, num_ctrls);
	if(ret)
		return ret;

	index = 0;
	while (1) {
		ret = arducam_write(client, CTRL_INDEX_REG, index);
		arducam_write(client, CTRL_VALUE_REG, 0);
		wait_for_free(client, 1);

		ret += arducam_read(client, CTRL_ID_REG, &id);
		ret += arducam_read(client, CTRL_MAX_REG, &max);
		ret += arducam_read(client, CTRL_MIN_REG, &min);
		ret += arducam_read(client, CTRL_DEF_REG, &def);
		ret += arducam_read(client, CTRL_STEP_REG, &step);
		if (ret < 0)
			goto err;
		if (id == NO_DATA_AVAILABLE || max == NO_DATA_AVAILABLE ||
			min == NO_DATA_AVAILABLE || def == NO_DATA_AVAILABLE ||
			step == NO_DATA_AVAILABLE)
			break;

		if (arducam_ctrl_get_name(id) != NULL) {
			priv->ctrls[index] = v4l2_ctrl_new_arducam(ctrl_hdlr,
						&arducam_ctrl_ops, id, min, max, step, def);
			v4l2_dbg(1, debug, priv->client, "%s: new custom ctrl, ctrl: %p.\n",
				__func__, priv->ctrls[index]);
		} else {
			v4l2_dbg(1, debug, priv->client, "%s: index = %x, id = %x, max = %x, min = %x\n",
					__func__, index, id, max, min);
			priv->ctrls[index] = v4l2_ctrl_new_std(ctrl_hdlr,
						&arducam_ctrl_ops, id,
						min, max, step, def);
			v4l2_dbg(1, debug, priv->client, "%s: ctrl: %p\n",
					__func__, priv->ctrls[index]);
		}

		switch(id) {
		case V4L2_CID_HFLIP:
			priv->hflip = priv->ctrls[index];
			if (priv->bayer_order_volatile)
				priv->hflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
			break;

		case V4L2_CID_VFLIP:
			priv->vflip = priv->ctrls[index];
			if (priv->bayer_order_volatile)
				priv->vflip->flags |= V4L2_CTRL_FLAG_MODIFY_LAYOUT;
			break;

		case V4L2_CID_HBLANK:
			priv->ctrls[index]->flags |= V4L2_CTRL_FLAG_READ_ONLY;
			break;
		}

		index++;
	}
	
	arducam_write(client, CTRL_INDEX_REG, 0);

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto err;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &arducam_ctrl_ops,
					      &props);
	if (ret)
		goto err;

	priv->sd.ctrl_handler = ctrl_hdlr;
	v4l2_ctrl_handler_setup(ctrl_hdlr);
	return 0;
err:
	return -ENODEV;
}

static int arducam_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	struct arducam *arducam;
    u32 device_id;
	u32 firmware_version;
	int ret;
	arducam = devm_kzalloc(&client->dev, sizeof(*arducam), GFP_KERNEL);
	if (!arducam)
		return -ENOMEM;
	/* Initialize subdev */
	v4l2_i2c_subdev_init(&arducam->sd, client, &arducam_subdev_ops);
	arducam->client = client;

	/* Get CSI2 bus config */
	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(&client->dev),
						  NULL);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &arducam->ep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Could not parse endpoint\n");
		return ret;
	}

	/* Get system clock (xclk) */
	arducam->xclk = devm_clk_get(dev, "xclk");
	if (IS_ERR(arducam->xclk)) {
		dev_err(dev, "failed to get xclk\n");
		return PTR_ERR(arducam->xclk);
	}
	arducam->xclk_freq = clk_get_rate(arducam->xclk);
	if (arducam->xclk_freq != 24000000) {
		dev_err(dev, "xclk frequency not supported: %d Hz\n",
			arducam->xclk_freq);
		return -EINVAL;
	}
	ret = arducam_get_regulators(arducam);
	if (ret)
		return ret;

	/* Request optional enable pin */
	arducam->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);


		/*
	 * The sensor must be powered for imx219_identify_module()
	 * to be able to read the CHIP_ID register
	 */
	ret = arducam_power_on(dev);
	if (ret)
		return ret;
	ret = arducam_read(client, DEVICE_ID_REG, &device_id);
	if (ret || device_id != DEVICE_ID) {
		dev_err(&client->dev, "probe failed\n");
		ret = -ENODEV;
		goto error_power_off;
	}

	ret = arducam_read(client, DEVICE_VERSION_REG, &firmware_version);
	if (ret) {
		dev_err(&client->dev, "read firmware version failed\n");
	}
	dev_info(&client->dev, "firmware version: 0x%04X\n", firmware_version);

	if (arducam_enum_pixformat(arducam)) {
		dev_err(&client->dev, "enum pixformat failed.\n");
		ret = -ENODEV;
		goto error_power_off;
	}
	
	arducam_write_reg(arducam, arducam_REG_MODE_SELECT,
				arducam_REG_VALUE_32BIT, arducam_MODE_STREAMING);
	
	wait_for_free(arducam->client, 5);

	if (arducam_enum_controls(arducam)) {
		dev_err(dev, "enum controls failed.\n");
		ret = -ENODEV;
		goto error_power_off;
	}

	arducam_write_reg(arducam, arducam_REG_MODE_SELECT,
				arducam_REG_VALUE_32BIT, arducam_MODE_STANDBY);

	/* Initialize subdev */
	arducam->sd.internal_ops = &arducam_internal_ops;
	arducam->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	arducam->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	/* Initialize source pad */
	arducam->pad[IMAGE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	arducam->pad[METADATA_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&arducam->sd.entity, NUM_PADS, arducam->pad);
	if (ret)
		goto error_handler_free;

	ret = v4l2_async_register_subdev_sensor_common(&arducam->sd);
	if (ret < 0)
		goto error_media_entity;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&arducam->sd.entity);

error_handler_free:
	arducam_free_controls(arducam);

error_power_off:
	arducam_power_off(dev);

	return ret;
}

static int arducam_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct arducam *arducam = to_arducam(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	arducam_free_controls(arducam);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct dev_pm_ops arducam_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(arducam_suspend, arducam_resume)
	SET_RUNTIME_PM_OPS(arducam_power_off, arducam_power_on, NULL)
};

static const struct of_device_id arducam_dt_ids[] = {
	{ .compatible = "arducam, arducam-pivariety" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, arducam_dt_ids);

static struct i2c_driver arducam_i2c_driver = {
	.driver = {
		.name = "arducam-pivariety",
		.of_match_table	= arducam_dt_ids,
		.pm = &arducam_pm_ops,
	},
	.probe = arducam_probe,
	.remove = arducam_remove,
};

module_i2c_driver(arducam_i2c_driver);

MODULE_AUTHOR("Arducam <www.arducam.com>");
MODULE_DESCRIPTION("Arducam sensor v4l2 driver");
MODULE_LICENSE("GPL v2");
