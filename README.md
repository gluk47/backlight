This tool provides control over lid brightness relying on the files «brightness*» in some directory inside the /sys/devices/ directory.

Since this tool writes to a device file with permissions 644 owned by root, it requires SUID bit.

To compile and install, say: make indirect-install

You'll need g++-4.6 or newer to succeed compilation, since I used C++11 (-std=c++11).

To get manual of usage, type backlight -h:

    Backlight 1.4.8, the laptop display brightness control.

    Invocation: backlight [params] action [arg]
            params may be before or after an action;
            an arg may be separated from an action by space.

    Parameters:
    -q      quiet mode, without messages for user, but with error reports to stderr.
    -p      use percents, if possible [default if max. brightness reported > 100].
    -a      use absolute values of brightness [default otherwise].
    -h      this help.
    -v      version (this is v1.4.8).

    Actions:
    =, d, display   display current brightness rate. This value can be used later as an argument to the '=' action.
    +               increase brightness.
    -               decrease brightness.
    =<value>        set brightness. You can get a correct value from a previous call with the action 'display'. Depending on the system this value is either an absolute number (machine-dependent) or a percent value. If the system does report max possible value more than 100, this parameter must be specified as a percent value, otherwise you'll see the warning from the action 'display', and this parameter must be specified as an absolute value. If the system reports max. value, you can force using percent or absolute value using -a or -p (see above).
    r, reconfigure          recreate configuration file. This file is created automatically if absent.

    Examples:
    backlight -
    backlight +
    backlight =
    backlight =50
    B="$(backlight -q =)"; sudo pm-suspend; backlight =$B

    Config file is /etc/backlight, which contains a single line, which is the path to the folder with control files such as «brightness». It is updated automatically if the program is unable to open a necessary file. This program may fail to autodetect a directory. In this case feel free to fix the config file manually.

Bitcoin address for donations: 17uBDmcXgjxzYm9crf4vVr2DGAazNR7Xae. Thank you ^_^ 
