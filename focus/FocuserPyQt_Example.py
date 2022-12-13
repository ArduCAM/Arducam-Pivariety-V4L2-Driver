from PyQt5 import QtWidgets, QtGui, QtCore
import subprocess
import time
from Focuser import Focuser


class FocuserExample(QtWidgets.QMainWindow):

    def __init__(self):
        super().__init__()
        self.setMinimumSize(QtCore.QSize(800, 600))
        self.setMaximumSize(QtCore.QSize(800, 600))
        self.setWindowTitle('Focus Example')
        self.setStyleSheet("background: #1B1E28;\n"
"font-family: \'Lato\';\n"
"font-style: normal;\n"
"font-weight: 400;\n"
"font-size: 16px;\n"
"line-height: 17px;\n"
"color: #D1D8EA;")

        self.mainWidget = QtWidgets.QWidget()
        self.setCentralWidget(self.mainWidget)

        self.layout = QtWidgets.QHBoxLayout(self.mainWidget)

        self.previewWidget = QtWidgets.QWidget(self.mainWidget)
        self.previewWidget.setStyleSheet("border: 1px solid #27385E;\n"
"border-radius: 10px;")
        
        self.focusSlider = QtWidgets.QSlider(self.mainWidget)
        self.focusSlider.setMinimumSize(QtCore.QSize(150, 40))
        self.focusSlider.setMaximumSize(QtCore.QSize(150, 40))
        self.focusSlider.setCursor(QtGui.QCursor(QtCore.Qt.PointingHandCursor))
        self.focusSlider.setMouseTracking(True)
        self.focusSlider.setTabletTracking(True)
        self.focusSlider.setStyleSheet("border: 3px solid #27389E;\n"
"border-radius: 0px;")
        self.focusSlider.setMinimum(0)
        self.focusSlider.setMaximum(1023)
        self.focusSlider.setSingleStep(50)
        self.focusSlider.setOrientation(QtCore.Qt.Horizontal)
        self.focusSlider.setTickPosition(QtWidgets.QSlider.TicksBelow)
        self.focusSlider.setTickInterval(50)
        self.focusSlider.setObjectName("focusSlider")
        self.focusSlider.valueChanged.connect(self.changeFocus)

        self.layout.addWidget(self.previewWidget)
        self.layout.addWidget(self.focusSlider)

        self.previewLayout = QtWidgets.QHBoxLayout(self.previewWidget)

    def startPreview(self):
        self.process = subprocess.Popen("libcamera-hello -t 0 -p 0,0,640,480 --viewfinder-width 1280  --viewfinder-height 720", shell=True)
        time.sleep(2)
        previewWindow = QtGui.QWindow.fromWinId(0x2400002)
        previewContainer = QtWidgets.QWidget.createWindowContainer(previewWindow)
        previewContainer.setMinimumSize(QtCore.QSize(640, 480))
        self.previewLayout.addWidget(previewContainer)

    def startFocuser(self):
        try:
            focuser = Focuser('/dev/v4l-subdev1')
            focuser.step = 1
        finally:
            self.focusSlider.setValue(focuser.get(Focuser.OPT_FOCUS))
    
    def changeFocus(self, value):
        try:
            focuser = Focuser('/dev/v4l-subdev1')
            focuser.step = 1
        finally:
            if value > focuser.get(Focuser.OPT_FOCUS):
                focuser.set(Focuser.OPT_FOCUS, value)
            elif value < focuser.get(Focuser.OPT_FOCUS):
                focuser.set(Focuser.OPT_FOCUS, value)


if __name__ == "__main__":
    import sys
    app = QtWidgets.QApplication(sys.argv)
    window = FocuserExample()
    window.show()
    window.startPreview()
    sys.exit(app.exec_())