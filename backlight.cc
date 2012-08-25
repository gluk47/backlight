#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <cstring>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

const string version = "1.4.4";

void assert_writable (const std::string& _filename) noexcept (false) {
    int fd = ::open(_filename.c_str(), O_WRONLY);
    if (fd == -1)
        throw runtime_error ("Unable to write to the file «" + _filename + "»:\n"
                             + strerror(errno)
                             + "\n\n\e[4mConsider using sudo, if it's available.\e[0m");
    ::close(fd);
}

unique_ptr<fstream> fopen (const string& _, decltype(ios::in | ios::out) _mode) noexcept (false) {
    if (::access (_.c_str(), F_OK)) throw runtime_error ("The file «" + _ + "» does not exist");
    if (_mode & ios::out) assert_writable(_);
    /*map<decltype(ios::in), decltype(R_OK)> openmodes {
        {ios::in, R_OK},
        {ios::out, W_OK},
        {ios::in | ios::out, R_OK | W_OK}
    };
    // access does not work correctly with suid bit
    if (::access(_.c_str(), openmodes[_mode])) throw runtime_error("Access denied to the file " + _);*/
    return unique_ptr<fstream> (new fstream(_.c_str(), _mode));
}

struct config;
ostream& operator<< (ostream&, const config&);
istream& operator>> (istream&, config&) noexcept (false);

struct config {
    string rcfile;
    const string& DataPath() const noexcept { return data_path; };
    const string& MaxPath() const noexcept { return max_path; }
    const string& CurrentPath() const noexcept { return current_path; }

    void DataPath(const string& _) { data_path = _; update_paths(); }

    static config& the() {
        static config gifnoc;
        return gifnoc;
    }
    bool quiet; ///< omit user messages, for script invocation
    void reconfigure() noexcept (false) {
        #define TMPFILE "/tmp/backlightrc.tmp"
        // filter = "|grep drm"
        auto find_cmd = [](const string& filter) {
            return "path=\"`find /sys/devices/ -name max_brightness"
                 + filter
                 + "|head -n1`\"";
        };
        string script = "if " + find_cmd ("|grep drm") + "; then :;\n"
                    + "elif " + find_cmd ("|grep acpi") + "; then :;\n"
                    + "else " + find_cmd ("") + ";\n"
                        "fi;\ndirname \"$path\"";
//         cerr << "script: «" + script + "»\n";
        system ((script + "> " TMPFILE).c_str());
        ifstream cfg(TMPFILE);
        cfg >> *this;
        if (not quiet)
            cerr << "\e[36;1m=== AutoDetected control directory: ===\e[0m\n"
                    "\t«"+ data_path + "».\n"
                    "Fix the file «" + rcfile + "» if it's wrong.\n\n";
        system(("mv -f " TMPFILE " " + rcfile + " 2>/dev/null || rm -f " TMPFILE).c_str());
        #undef TMPFILE
    }
private:
    void update_paths() {
        max_path = data_path + "/max_brightness";
        current_path = data_path + "/brightness";
    }
    string data_path; ///< path to control files
    string max_path; ///< control file, containing max value (full path)
    string current_path; ///< control file, containing actual value (full path)
    config() noexcept (false):
               data_path("/sys/class/backlight/acpi_video0"),
                       rcfile("/etc/backlight"),
               quiet(false) {
                   update_paths();
                   auto read_config=[this](){
                       ifstream rc(rcfile);
                       rc.exceptions(rc.badbit|rc.failbit);
                       rc >> *this;
                   };
                   try {
                       read_config();
                   } catch (exception& x) {
                       //cout << "Failed reading config file " << rcfile << ".\nTrying to save new config there...\n";
                       try { reconfigure(); }
                       catch (exception& x) {
                         //  cout << "\e[31;1mFailed reading config again.\e[0m Check if you have writing permissions to " << rcfile << ".\n\nConsider using sudo if it's available. You'll need superuser rights anyway in order to write to the control file /sys/<...>/brightness_now.\n";
                           ;
                       }
                   }
               }
};

ostream& operator<< (ostream&, const config&) {
}

istream& operator>> (istream& _str, config& _cfg) noexcept (false) {
    char filename[65536];
    _str.getline(filename, 65536);
    _cfg.DataPath(filename);
}

struct brightness {
    int max() noexcept (false) { if (not _Max_read) _Read_max(); return _Max;}
    int now() noexcept (false) { return _Read(config::the().CurrentPath());}
    inline int now_percent() throw (exception) { return max()? now() * 100 / max() : now(); }
    inline int one_percent() throw (exception) { return max()? max() / 100 : 1; }
    static brightness& the() { static brightness _; return _; }
    void now(int _) {
        if (max() != 0) {
            if (_ > 100) _ = 100;
            else if (_ < 0) _ = 0;
            _ = max() * _ / 100 + 1;
        } else {
            if (_ > max()) _ = max();
            else if (_ < 0) _ = 0;
        }
        _Modify ([_](int& _now){_now = _;});
    }
    int operator++ () noexcept (false) {
        return now() >= max()? max() : _Modify([this](int& _) {
            _ = max() * (now_percent() + 1) / 100 + 1;
        });
    }
    int operator-- () noexcept (false) {
        return now() <= 0? 0 : _Modify([this](int& _){
            _ = max() * (now_percent() - 1) / 100 + 1;
        });
    }
private:
    brightness() 
             :  _Max(-1), _Max_read(false) {}
    int _Max;
    bool _Max_read;
    int _Read(const std::string& _) {
        auto str = fopen(_, ios::in);
        str->exceptions(ios::badbit | ios::failbit);
        int i;
        *str >> i;
        return i;
    }
    /** 1. Read current brightness value.
     *  2. Call _modifier with this value passed by reference.
     *  3. Save the result back to file.
    **/
    int _Modify(std::function<void (int&)>&& _modifier) {
#if 0
        /// fix samsung brightness, restore it to max.
        /// my acpi brightness does not work again, this code snippet is useless. drm brightness device works well.
        try {
            auto max_str = fopen ("/sys/class/backlight/samsung/max_brightness",
                                  ios::in);
            auto str = fopen ("/sys/class/backlight/samsung/brightness",
                              ios::in | ios::out);
            int m;
            int s;
            *str >> s;
            *max_str >> m;
            if (s != m) {
                *str << m;
                return this->now();
            }
        } catch (...) {
        }
#endif
        int now = this->now();
        auto str = fopen (config::the().CurrentPath(), ios::out);
        str->exceptions (ios::badbit | ios::failbit);
        _modifier(now);
        *str << now;

        return now;
    }
    void _Read_max() noexcept (false) { _Max = _Read(config::the().MaxPath()); _Max_read = true; }
};

void display_brightness() {
    if (config::the().quiet) {
        cout << brightness::the().now_percent() << "\n";
        return;
    }
    auto max = []{return brightness::the().max(); };
    auto now = []{return brightness::the().now(); };
    auto now_percent = []{return brightness::the().now_percent(); };
    if (max() == 0) 
        cout << "Brightness level is " << now() << ", maximum is reported to be 0 (most likely just unknown)\n";
    else 
        cout << "Brightness is about " << now_percent() << "%\n";
}

void print_help() {
    cout << "Backlight " << version << ", the laptop display brightness control.\n"
            "\n\e[4mInvocation:\e[0m backlight [params] action [arg]\n"
            "\tparams may be before or after an action;\n"
            "\tan arg may be separated from an action by space.\n\n"
            "\e[4mParameters:\e[0m\n"
            "\e[1m-q\e[0m\tquiet mode, without messages for user, but with error reports to stderr.\n"
            "\e[1m-h\e[0m\tthis help.\n"
            "\e[1m-v\e[0m\tversion (this is v" << version << ").\n"
            "\n\e[4mActions:\e[0m\n"
            "\e[1m=, d, display\e[0m\tdisplay current brightness rate. This value can be used later as an argument to the '=' action.\n"
            "\e[1m+\e[0m\t\tincrease brightness.\n"
            "\e[1m-\e[0m\t\tdecrease brightness.\n"
            "\e[1m=<value>\e[0m\tset brightness. You can get a correct value from a previous call with the action 'display'. Depending on the system this value is either an absolute number (machine-dependent) or a percent value. If the system does report max possible value, this parameter must be specified as a percernt value, otherwise you'll see the warning from the action 'display', and this parameter must be specified as an absolute value.\n"
            "\n\e[4mExamples:\e[0m\n"
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
void perform_action(int argc, char* argv[]) {
    int c;
    bool no_action = false,
         need_help = false;
    while((c = getopt(argc, argv, "hvq")) != -1) 
        switch(c){
        case 'h': need_help = no_action = true; break;
        case 'v': print_version(); no_action = true; break;
        case 'q': config::the().quiet = true; break;
        }

    if (need_help or not no_action and optind == argc) { print_help(); return; }
    if (no_action) return;
    for (; optind < argc; ++optind) {
        const string action = argv[optind];
        if (action == "display" or action == "d") 
            display_brightness();
        else if (action[0] == '+')
            ++brightness::the();
        else if (action[0] == '-')
            --brightness::the();
        else if (action[0] == '=') {
            if (action.length() == 1) { display_brightness(); continue; }
            stringstream ss (argv[optind] + 1);
            int request;
            ss >> request;
            brightness::the().now(request);
        }
        else cerr << "Action " << action << " is not defined\n";
    }
}

int main(int argc, char* argv[]) {
    try {
        perform_action(argc, argv);
        return 0;
    } catch (exception& x) {
        cerr << "epic fail: " << x.what() << "\n";
        return 1;
    }
}

