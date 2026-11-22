# UI Test

To run automated tests on firefox focus, you will need a few things to get started.

1. Working installation of git and a local clone of the this Focus repository. You can do:

         git clone https://github.com/mozilla-mobile/focus-android.git

2. For this walkthrough, you will also need Android Studio, which can be found at [android website](https://developer.android.com/studio) . Follow the instructions. For the most part, Android Studio is going to set itself up with the repository all you have to do is open the directory. You may also need to install JDK as well.

3. From there you will need either an android device (Preferably Nexus 4 or more recent) or setup emulators in Android Studio
To create an emulator:
    1. Open **Android Studio**, and navigate to the **Tools** drop down menu
    2. Navigate to **Android** pop out selection on the list and select "**AVD Manager**". A window will open from which you can manage your virtual devices
    3. select "**Create Virtual Device**"
    4. Select the phone tab for devices and select either the nexus devices or Google Pixel (preferably)
    5. you will need to select images with API level 22 ~ 26, download and install them by selecting download next to their names.
    6. Select the images to be installed, and click next.
    7. You can set the name of the device and the configuration, but is recommended to set following configurations changed:
       * **Camera: None (For both Front and Back)**
       * **Graphics: Automatic**
       * **Multi-Core CPU: Unchecked**
    8. **Device Testing**: If you wish to run tests on device, you need to enable Developer Options in the Settings menu (this is usually achieved by tapping **About Phone** -> **Build Number** 7 times), and enable '**USB Debugging**.' Then connect the phone to PC via USB cable.

       Our CI currently uses the following HW/SW configuration:

       _Firefox Focus_
       * Nexus 6, API 23 simulator
       * Pixel 2, API 26 simulator

       _Firefox Klar_
       * Nexus 9, virtual, API Level 26

4. You are now ready to run the automated tests!
    1. Click the bottom left corner of Android Studio, and enable "**Build Variants**." The Build Variants value should be set to one of the following:

       on device: '**focusArmDebug**' or '**klarArmDebug**'

       simulator: '**focusX86Debug**' or '**klarX86Debug**'

5. Click on the drop down on the top left of Android Studio below the path view, and select '**Tests**'
6. navigate the sub-directories to the folder locations of the tests to be run. For example, UI Tests are located in **focus-android/app/src/androidTest/java/org.mozilla.focus.activity**
7. To run all tests in folder right click on the folder and select run. For individual tests you will do the same but right click on the specific test.
8. select your simulator or device (connected via ADB).

From there Android Studio will compile and install Focus on the simulator or device and start running the tests.

If you just need to run Focus, select '**app**' from the **Configurations** pull-down menu and press Run button.

You may need to follow along the first time to grant the app permissions to access the different parts of android such as storage access or the tests will fail, or manually turn on Storage permissions for Focus.

It should be noted that not all tests will pass in simulator environment. Those tests are designed to run on our CI setup, and while most will not have issues, **BadURLTest** will fail on vanilla simulator environment where no Google Play store is installed, because it'll try to decode market:// url. Also, **AdBlockingTest** is suppressed, because while it passed locally, it was failing on our CI due to the network setup. I have left the test script intact for future references.

(Drafted by [1Jamie](https://github.com/1jamie))
