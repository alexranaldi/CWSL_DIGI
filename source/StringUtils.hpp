#include <string>
#include <vector>
#include <sstream>
#include <algorithm> 

// trim from start (in place)
static inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string& s) {
    ltrim(s);
    rtrim(s);
}

// trim from start (copying)
static inline std::string ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

// trim from end (copying)
static inline std::string rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

// trim from both ends (copying)
static inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

static inline std::vector<std::string> splitStringByDelim(const std::string& input, const char delim, bool noEmpty=false) {
    std::istringstream iss(input);
    std::vector<std::string> v;
    std::string s;
    while (std::getline(iss, s, delim)) {
        if (noEmpty) {
            bool all = true;
            for (size_t c = 0; c < s.size(); ++c) {
                if (s.at(c) != delim) {
                    all = false;
                    break;
                }
            }
            if (all) {
                continue;
            }
        }
        v.push_back(s);
    }
    return v;
}
