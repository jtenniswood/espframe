#pragma once
#include <string>

static constexpr const char *MONTH_NAMES[] = {
  "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

inline std::string strip_trailing_slashes(const std::string &url) {
  std::string result = url;
  while (!result.empty() && result.back() == '/') result.pop_back();
  return result;
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

inline std::string format_photo_date(int year, int month) {
  if (month >= 1 && month <= 12)
    return std::string(MONTH_NAMES[month]) + " " + std::to_string(year);
  return "";
}
