#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <cstring>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

const string version = "1.4.8";

/**
 * @brief Check that the file is writable. Throw runtime_error if not
 * @throws std::runtime_error if failed to open @c _filename for writing
 */
void assert_writable (const std::string& _filename) noexcept (false) {
    int fd = ::open (_filename.c_str(), O_WRONLY);
    if (fd == -1)
        throw runtime_error ("Unable to write to the file «"
                             + _filename + "»:\n"
                             + strerror (errno)
                             + "\n\n\e[4mConsider using sudo, if it's available.\e[0m");
    ::close (fd);
}

/**
 * @brief Open the file or throw a descriptive message.
 * @throws std::runtime_error upon error
 */
unique_ptr<fstream> fopen (const string& _, decltype (ios::in | ios::out) _mode) noexcept (false) {
    if (::access (_.c_str(), F_OK))
        throw runtime_error ("The file «" + _ + "» does not exist");
    if (_mode & ios::out)
        assert_writable (_);
    /*map<decltype(ios::in), decltype(R_OK)> openmodes {
        {ios::in, R_OK},
        {ios::out, W_OK},
        {ios::in | ios::out, R_OK | W_OK}
    };
    // access does not work correctly with suid bit
    if (::access(_.c_str(), openmodes[_mode])) throw runtime_error("Access denied to the file " + _);*/
    return unique_ptr<fstream> (new fstream (_.c_str(), _mode));
}

struct config;
ostream& operator<< (ostream&, const config&);
istream& operator>> (istream&, config&) noexcept (false);

struct config {
    string rcfile = "/etc/backlight";
    const string& DataPath() const noexcept { return data_path; };
    const string& MaxPath() const noexcept { return max_path; }
    const string& CurrentPath() const noexcept { return current_path; }
    void DataPath (const string& _) {
        data_path = _;
        update_paths();
    }

    /// the singletone
    static config& the() {
        static config gifnoc;
        return gifnoc;
    }
    bool quiet = false; ///< omit user messages, for script invocation
    /// Autodetect control directory and overwrite existing config file
    void reconfigure() noexcept (false);
    bool UsePercents;
    bool ForcedUnits = false; ///< user forced percents or absolute values
private:
    void update_paths() {
        max_path = data_path + "/max_brightness";
        current_path = data_path + "/brightness";
    }
    string data_path; ///< path to control files
    string max_path; ///< control file, containing max value (full path)
    string current_path; ///< control file, containing actual value (full path)
    config () noexcept (false) :
        data_path ("/sys/class/backlight/acpi_video0"),
        rcfile ("/etc/backlight"),
        quiet (false) {
        update_paths();
        auto read_config = [this] {
            ifstream rc (rcfile);
            rc.exceptions (rc.badbit | rc.failbit);
            rc >> *this;
        };
        try {
            read_config();
        } catch (exception& x) {
            //cout << "Failed reading config file " << rcfile << ".\nTrying to save new config there...\n";
            try {
                reconfigure();
            } catch (exception& x) {
                //  cout << "\e[31;1mFailed reading config again.\e[0m Check if you have writing permissions to " << rcfile << ".\n\nConsider using sudo if it's available. You'll need superuser rights anyway in order to write to the control file /sys/<...>/brightness_now.\n";
                ;
            }
        }
    }
};

void config::reconfigure() {
    char fname [] = "/tmp/backlightrc.XXXXXX";
    if (mktemp(fname) == "") {
        perror ("Failed to created temporary file");
        return;
    }
    auto find_cmd = [] (const string & filter, const string & prefix) {
        return "path=\"`find /sys/devices -name "
               + prefix
               + "brightness | grep backlight "
               + filter
               + (not filter.empty()? " " : "")
               + "| head -n1`\"";
    };
    auto find1 = [&find_cmd] (const string & prefix) {
        // acpi sometimes does not work. drm works then (often).
        return find_cmd ("| grep drm", prefix) + " || \\\n"
               + find_cmd ("| grep acpi", prefix) + " || \\\n"
               + find_cmd ("", prefix);
    };
    string script = find1 ("max_") + " || \\\n" + find1 ("")
                    + "\ndirname \"$path\"";
//     cerr << "script:\n" + script + "\n\n";
    system ( (script + "> " + fname).c_str());
    ifstream cfg (fname);
    cfg >> *this;
    if (not quiet)
        cerr << "\e[36;1m=== AutoDetected control directory: ===\e[0m\n"
             "«" + data_path + "».\n"
             "Fix the file «" + rcfile + "» if it's wrong.\n\n";
    system ( (string ("mv -f ") + fname + " " + rcfile + " 2>/dev/null || { rm -f " + fname + "; echo \"Failed to write to «" + rcfile + "», use sudo if possible.\" ; }").c_str());
}

// ostream& operator<< (ostream&, const config&) {
// }

istream& operator>> (istream& _str, config& _cfg) noexcept (false) {
    char filename [65536];
    _str.getline (filename, 65536);
    _cfg.DataPath (filename);
}

struct brightness {
    /// max as driver's absolute value
    /// Auto-adjusts config::the().UsePercents
    int max() noexcept (false) {
        if (not _Max_read)
            _Read_max();
        return _Max;
    }
    /// absolute value of brightness as reported by driver
    int now() noexcept (false) {
        return _Read (config::the().CurrentPath());
    }
    /// brightness value for user
    inline int now_percent() throw (exception) {
        const auto m = max(); //< also adjusts config::the().UsePercents
        return config::the().UsePercents ?
               (assert (m), now() * 100 / m)
               : now();
    }
    /// 1 unit to change brightness
    inline int one_percent() throw (exception) {
        const auto m = max ();
        return config::the().UsePercents ?
               static_cast<int> (m / 100) // redundant cast, for readability's sake
               : 1;
    }
    static brightness& the() {
        static brightness _;
        return _;
    }
    /// set brightness.
    /// arg is either per cent value, or absolute, depending on config.
    void now (int _) {
        const auto m = max();
        if (config::the().UsePercents) {
            if (_ > 100)
                _ = 100;
            else if (_ < 0)
                _ = 0;
            const auto fullarg = m * _;
            _ = fullarg / 100 + static_cast<bool> (fullarg % 100);
        } else {
            if (_ > m)
                _ = m;
            else if (_ < 0)
                _ = 0;
        }
        _Modify ([_] (int & _now) {
            _now = _;
        });
    }
    inline void inc (unsigned shift) noexcept (false) { now (now_percent() + shift); }
    inline void dec (unsigned shift) noexcept (false) { now (now_percent() - shift); }
    // prefix versions only: modify brightness and return new value (in user scale)
    int operator++ () noexcept (false) {
        inc (1);
        return now_percent ();
    }
    int operator-- () noexcept (false) {
        dec (1);
        return now_percent ();
    }
#if 0
    void dump_stats () {
        cerr << "brightness object @" << std::hex << this << ":\n" << std::dec
             << "now: " << now () << "\n"
             "now_percent: " << now_percent () << "\n"
             "max: " << max () << "\n"
             "one_percent: " << one_percent () << "\n"
             "measure: " << (config::the().UsePercents ? "per cent" : "absolute") << "\n\n";
    }
#endif
private:
    int _Max = -1; ///< Max value as reported by driver (absolute int value, not normalized)
    bool _Max_read = false; ///< _Max filled. (Lazyness support).
    /** @brief read an int from the file named @c _
     * @throw std::exception upon failure
     * @return the value read
    **/
    int _Read (const std::string& _) {
        auto str = fopen (_, ios::in);
        str->exceptions (ios::badbit | ios::failbit);
        int i;
        *str >> i;
        return i;
    }
    /** 1. Read current brightness value.
     *  2. Call _modifier with this value passed by reference.
     *  3. Save the result back to file.
    **/
    int _Modify (std::function<void (int&) > && _modifier) {
        int now = this->now();
        auto str = fopen (config::the().CurrentPath(), ios::out);
        str->exceptions (ios::badbit | ios::failbit);
        _modifier (now);
        *str << now;

        return now;
    }
    void _Read_max() noexcept (false) {
        _Max = _Read (config::the ().MaxPath ());
        _Max_read = true;
        if (_Max == 0)
            config::the ().UsePercents = true;
        else if (not config::the ().ForcedUnits)
            config::the().UsePercents = _Max > 100;
    }
};

void display_brightness() {
    if (config::the().quiet) {
        cout << brightness::the().now_percent() << "\n";
        return;
    }
    // aliases
    const auto max = brightness::the().max();
    auto now = [] {return brightness::the().now(); };
    auto now_percent = [] {return brightness::the().now_percent(); };
    if (config::the().UsePercents)
        cout << "Brightness is about " << now_percent() << "%\n";
    else {
        cout << "Brightness level is " << now();
        if (max > 0)
            cout << " out of " << max << "\n";
        else
            cout << ", maximum is reported to be 0 (most likely just unknown)\n";
    }
}

void print_opt (string _opt, const string& _msg) {
    string lpad = 16 > _opt.size () ? string (16 - _opt.size (), ' ') : string ();
    const char* font_emph = "\e[1m";
    const char* font_normal = "\e[0m";
    if (not _opt.empty())
        _opt = font_emph + _opt + font_normal;
    cout << _opt << lpad << _msg << "\n";
}
void print_help() {
    cout << "Backlight " << version << ", the laptop display brightness control.\n"
         "\n\e[4mInvocation:\e[0m backlight [params] action [arg]\n"
         "   params may be before or after an action;\n"
         "   an arg may be separated from an action by space.\n\n"
         "\e[4mParameters:\e[0m\n";
    print_opt ("-q", "quiet mode, without messages for user, but with error reports to stderr.");
    print_opt ("-p", "use percents, if possible [default if max. brightness reported > 100].");
    print_opt ("-a", "use absolute values of brightness [default otherwise].");
    print_opt ("-d", "specify control directory with brightness values.\n"
               "       Useful when hardware provides different interfaces to control\n"
               "       the same screen more and less finely.");
    print_opt ("-h", "this help.");
    print_opt ("-v", "version (this is v" + version + ").");
    cout << "\n\e[4mActions:\e[0m\n";
    print_opt ("=, d, display", "display current brightness rate. This value can be used later as an argument to the '=' action.");
    print_opt ("+", "increase brightness.");
    print_opt ("-", "decrease brightness.");
    print_opt ("=<value>", "set brightness.\n"
               "            You can get a correct value from a previous call with the action 'display'. Depending on the system this value is either an absolute number (machine-dependent) or a percent value. If the system does report max possible value more than 100, this parameter must be specified as a percent value, otherwise you'll see the warning from the action 'display', and this parameter must be specified as an absolute value. If the system reports max. value, you can force using percent or absolute value using -a or -p (see above).");
    print_opt ("r, reconfigure", "\trecreate configuration file. This file is created automatically if absent.");
    cout << "\n\e[4mExamples:\e[0m\n"
         "backlight -\n"
         "backlight +\n"
         "backlight =\n"
         "backlight =50\n"
         "B=\"$(backlight -q =)\"; sudo pm-suspend; backlight =$B\n"
         "\n\e[4mConfig\e[0m file is " << config::the().rcfile << ", which contains a single line, which is the path to the folder with control files such as «brightness». It is updated automatically if the program is unable to open a necessary file. This program may fail to autodetect a directory. In this case feel free to fix the config file manually.\n";
}
void print_version() {
    cout << "Backlight v" << version << "\n";
}
void perform_action (int argc, char* argv[]) {
    int c;
    bool no_action = false,
         need_help = false;
    while ( (c = getopt (argc, argv, "hvqapd:")) != -1)
        switch (c) {
        case 'h':
            need_help = no_action = true;
            break;
        case 'v':
            print_version();
            no_action = true;
            break;
        case 'q':
            config::the().quiet = true;
            break;
        case 'a':
            config::the().UsePercents = false;
            config::the().ForcedUnits = true;
            break;
        case 'p':
            config::the().UsePercents = true;
            config::the().ForcedUnits = true;
            break;
        case 'd':
            config::the().DataPath (optarg);
            break;
        }
    if (need_help or not no_action and optind == argc) {
        print_help();
        return;
    }
    if (no_action)
        return;
    for (; optind < argc; ++optind) {
        const string action = argv[optind];
        if (action == "display" or action == "d" or action == "=")
            display_brightness();
        else if (action[0] == '+') {
            if (action.size() == 1)
                ++brightness::the();
            else {
                int amount = atoi (action.c_str() + 1);
                brightness::the().inc(amount);
            }
        } else if (action[0] == '-') {
            if (action.size() == 1)
                -- brightness::the();
            else {
                int amount = atoi (action.c_str() + 1);
                brightness::the().dec(amount);
            }
        }
        else if (action[0] == '=') {
            assert (action.size() > 1); //< this case is handled earlier
            stringstream ss (argv[optind] + 1);
            int request;
            ss >> request;
            brightness::the().now (request);
        } else if (action == "reconfigure" or action == "r")
            config::the().reconfigure();
        else
            cerr << "Action " << action << " is not defined\n";
    }
}

int main (int argc, char* argv[]) {
    try {
        perform_action (argc, argv);
        return 0;
    } catch (exception& x) {
        cerr << "epic fail: " << x.what() << "\n";
        return 1;
    }
}
