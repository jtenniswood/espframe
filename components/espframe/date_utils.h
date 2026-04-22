#pragma once
#include <string>

// Small formatting helpers shared by the display metadata code. They keep the
// YAML lambdas focused on flow control instead of date/string manipulation.
static constexpr const char *MONTH_NAMES[] = {
  "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static constexpr const char *MONTH_NAMES_FULL[] = {
  "", "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};

inline std::string strip_trailing_slashes(const std::string &url) {
  std::string result = url;
  size_t min_len = 0;
  size_t scheme_pos = result.find("://");
  if (scheme_pos != std::string::npos) min_len = scheme_pos + 3;
  while (result.size() > min_len && result.back() == '/') result.pop_back();
  return result;
}

inline bool is_ascii_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

inline bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

inline std::string trim_ascii_whitespace(const std::string &input) {
  size_t start = 0;
  while (start < input.size() && is_ascii_space(input[start])) start++;
  size_t end = input.size();
  while (end > start && is_ascii_space(input[end - 1])) end--;
  return input.substr(start, end - start);
}

inline std::string to_lower_ascii(std::string value) {
  for (char &c : value) {
    if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
  }
  return value;
}

inline bool ends_with_ascii(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool url_has_scheme(const std::string &url) {
  return url.find("://") != std::string::npos;
}

inline bool is_http_url(const std::string &url) {
  std::string lower = to_lower_ascii(url);
  return lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0;
}

inline std::string extract_url_authority(const std::string &url_without_scheme) {
  size_t start = 0;
  if (url_without_scheme.rfind("//", 0) == 0) start = 2;
  size_t end = url_without_scheme.find_first_of("/?#", start);
  if (end == std::string::npos) return url_without_scheme.substr(start);
  return url_without_scheme.substr(start, end - start);
}

inline std::string extract_url_host(const std::string &url_without_scheme) {
  std::string authority = extract_url_authority(url_without_scheme);
  size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);
  if (authority.empty()) return "";

  if (authority[0] == '[') {
    size_t close = authority.find(']');
    return to_lower_ascii(close == std::string::npos ? authority : authority.substr(0, close + 1));
  }

  size_t colon = authority.find(':');
  return to_lower_ascii(authority.substr(0, colon));
}

inline bool url_authority_has_port(const std::string &url_without_scheme) {
  std::string authority = extract_url_authority(url_without_scheme);
  size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);
  if (authority.empty()) return false;

  if (authority[0] == '[') {
    size_t close = authority.find(']');
    return close != std::string::npos && close + 1 < authority.size() && authority[close + 1] == ':';
  }

  return authority.find(':') != std::string::npos;
}

inline std::string extract_url_port(const std::string &url_without_scheme) {
  std::string authority = extract_url_authority(url_without_scheme);
  size_t at = authority.rfind('@');
  if (at != std::string::npos) authority = authority.substr(at + 1);
  if (authority.empty()) return "";

  size_t port_start = std::string::npos;
  if (authority[0] == '[') {
    size_t close = authority.find(']');
    if (close != std::string::npos && close + 1 < authority.size() && authority[close + 1] == ':')
      port_start = close + 2;
  } else {
    size_t colon = authority.find(':');
    if (colon != std::string::npos) port_start = colon + 1;
  }

  if (port_start == std::string::npos || port_start >= authority.size()) return "";
  size_t port_end = port_start;
  while (port_end < authority.size() && is_ascii_digit(authority[port_end])) port_end++;
  return authority.substr(port_start, port_end - port_start);
}

inline bool is_ipv4_literal_host(const std::string &host) {
  int dot_count = 0;
  int part_value = 0;
  int part_digits = 0;
  for (char c : host) {
    if (is_ascii_digit(c)) {
      part_value = part_value * 10 + (c - '0');
      part_digits++;
      if (part_digits > 3 || part_value > 255) return false;
    } else if (c == '.') {
      if (part_digits == 0) return false;
      dot_count++;
      part_value = 0;
      part_digits = 0;
    } else {
      return false;
    }
  }
  return dot_count == 3 && part_digits > 0;
}

inline bool is_local_immich_host(const std::string &host) {
  if (host.empty()) return false;
  if (host == "localhost") return true;
  if (host[0] == '[') return true;  // IPv6 literals are usually local direct connections.
  if (is_ipv4_literal_host(host)) return true;
  return ends_with_ascii(host, ".local") || ends_with_ascii(host, ".lan");
}

inline std::string normalize_immich_base_url(const std::string &input) {
  std::string url = trim_ascii_whitespace(input);
  if (url.empty()) return "";

  if (url.rfind("//", 0) == 0) {
    url = "https:" + url;
  } else if (!url_has_scheme(url)) {
    std::string host = extract_url_host(url);
    std::string port = extract_url_port(url);
    bool use_http = is_local_immich_host(host) || url_authority_has_port(url);
    if (port == "443") use_http = false;
    url = std::string(use_http ? "http://" : "https://") + url;
  } else {
    size_t scheme_pos = url.find("://");
    std::string scheme = to_lower_ascii(url.substr(0, scheme_pos));
    url = scheme + url.substr(scheme_pos);
  }

  return strip_trailing_slashes(url);
}

inline std::string format_time_ago(int photo_year, int photo_month, int now_year, int now_month) {
  if (photo_year <= 0) return "";
  int months_ago = (now_year - photo_year) * 12 + (now_month - photo_month);
  if (months_ago >= 12) {
    int years = months_ago / 12;
    if (years == 1) return "1 year ago";
    return std::to_string(years) + " years ago";
  }
  if (months_ago == 1) return "1 month ago";
  if (months_ago > 1) return std::to_string(months_ago) + " months ago";
  return "";
}

inline bool is_valid_date_parts(int year, int month, int day) {
  return year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

inline int days_from_civil(int year, int month, int day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned m = static_cast<unsigned>(month);
  const unsigned d = static_cast<unsigned>(day);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

inline std::string plural_time_ago(int value, const char *unit) {
  if (value == 1) return std::string("1 ") + unit + " ago";
  return std::to_string(value) + " " + unit + "s ago";
}

inline std::string format_photo_age(int photo_year, int photo_month, int photo_day,
                                    int now_year, int now_month, int now_day) {
  if (!is_valid_date_parts(photo_year, photo_month, photo_day) ||
      !is_valid_date_parts(now_year, now_month, now_day)) {
    return "";
  }

  int days_ago = days_from_civil(now_year, now_month, now_day) -
                 days_from_civil(photo_year, photo_month, photo_day);
  if (days_ago < 0) days_ago = 0;
  if (days_ago == 0) return "today";

  if (days_ago >= 365) {
    return plural_time_ago((days_ago + 182) / 365, "year");
  }
  if (days_ago >= 30) {
    int months = (days_ago + 15) / 30;
    if (months >= 12) return "1 year ago";
    return plural_time_ago(months, "month");
  }
  return plural_time_ago(days_ago, "day");
}

inline std::string format_photo_date(int year, int month) {
  if (month >= 1 && month <= 12)
    return std::string(MONTH_NAMES[month]) + " " + std::to_string(year);
  return "";
}

inline std::string format_photo_date_full(int year, int month, int day) {
  if (!is_valid_date_parts(year, month, day)) return "";
  return std::to_string(day) + " " + std::string(MONTH_NAMES_FULL[month]) + ", " + std::to_string(year);
}
