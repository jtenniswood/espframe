// ESPFRAME: generated from docs/webserver/src and product/espframe.json; run `npm run generate` to update.
(function () {
  "use strict";

  var TIMEZONES = ["Pacific/Midway (GMT-11)","Pacific/Pago_Pago (GMT-11)","Pacific/Honolulu (GMT-10)","America/Adak (GMT-10)","America/Anchorage (GMT-9)","America/Juneau (GMT-9)","America/Los_Angeles (GMT-8)","America/Vancouver (GMT-8)","America/Tijuana (GMT-8)","America/Denver (GMT-7)","America/Phoenix (GMT-7)","America/Edmonton (GMT-7)","America/Boise (GMT-7)","America/Chicago (GMT-6)","America/Mexico_City (GMT-6)","America/Winnipeg (GMT-6)","America/Guatemala (GMT-6)","America/Costa_Rica (GMT-6)","America/New_York (GMT-5)","America/Toronto (GMT-5)","America/Detroit (GMT-5)","America/Havana (GMT-5)","America/Bogota (GMT-5)","America/Lima (GMT-5)","America/Jamaica (GMT-5)","America/Panama (GMT-5)","America/Halifax (GMT-4)","America/Caracas (GMT-4)","America/Santiago (GMT-4)","America/La_Paz (GMT-4)","America/Manaus (GMT-4)","America/Barbados (GMT-4)","America/Puerto_Rico (GMT-4)","America/Santo_Domingo (GMT-4)","America/St_Johns (GMT-3:30)","America/Sao_Paulo (GMT-3)","America/Argentina/Buenos_Aires (GMT-3)","America/Montevideo (GMT-3)","America/Paramaribo (GMT-3)","Atlantic/South_Georgia (GMT-2)","Atlantic/Azores (GMT-1)","Atlantic/Cape_Verde (GMT-1)","UTC (GMT+0)","Europe/London (GMT+0)","Europe/Dublin (GMT+0)","Europe/Lisbon (GMT+0)","Africa/Casablanca (GMT+1)","Africa/Accra (GMT+0)","Atlantic/Reykjavik (GMT+0)","Europe/Paris (GMT+1)","Europe/Berlin (GMT+1)","Europe/Rome (GMT+1)","Europe/Madrid (GMT+1)","Europe/Amsterdam (GMT+1)","Europe/Brussels (GMT+1)","Europe/Vienna (GMT+1)","Europe/Zurich (GMT+1)","Europe/Stockholm (GMT+1)","Europe/Oslo (GMT+1)","Europe/Copenhagen (GMT+1)","Europe/Warsaw (GMT+1)","Europe/Prague (GMT+1)","Europe/Budapest (GMT+1)","Europe/Belgrade (GMT+1)","Africa/Lagos (GMT+1)","Africa/Tunis (GMT+1)","Africa/Cairo (GMT+2)","Europe/Athens (GMT+2)","Europe/Bucharest (GMT+2)","Europe/Helsinki (GMT+2)","Europe/Kyiv (GMT+2)","Europe/Istanbul (GMT+3)","Africa/Johannesburg (GMT+2)","Africa/Nairobi (GMT+3)","Asia/Jerusalem (GMT+2)","Asia/Amman (GMT+3)","Asia/Beirut (GMT+2)","Europe/Moscow (GMT+3)","Asia/Baghdad (GMT+3)","Asia/Riyadh (GMT+3)","Asia/Kuwait (GMT+3)","Asia/Qatar (GMT+3)","Africa/Addis_Ababa (GMT+3)","Asia/Tehran (GMT+3:30)","Asia/Dubai (GMT+4)","Asia/Muscat (GMT+4)","Asia/Baku (GMT+4)","Asia/Tbilisi (GMT+4)","Indian/Mauritius (GMT+4)","Asia/Kabul (GMT+4:30)","Asia/Karachi (GMT+5)","Asia/Tashkent (GMT+5)","Asia/Yekaterinburg (GMT+5)","Asia/Kolkata (GMT+5:30)","Asia/Colombo (GMT+5:30)","Asia/Kathmandu (GMT+5:45)","Asia/Dhaka (GMT+6)","Asia/Almaty (GMT+5)","Asia/Rangoon (GMT+6:30)","Asia/Bangkok (GMT+7)","Asia/Jakarta (GMT+7)","Asia/Ho_Chi_Minh (GMT+7)","Asia/Singapore (GMT+8)","Asia/Kuala_Lumpur (GMT+8)","Asia/Shanghai (GMT+8)","Asia/Hong_Kong (GMT+8)","Asia/Taipei (GMT+8)","Asia/Manila (GMT+8)","Australia/Perth (GMT+8)","Asia/Tokyo (GMT+9)","Asia/Seoul (GMT+9)","Asia/Pyongyang (GMT+9)","Australia/Adelaide (GMT+9:30)","Australia/Darwin (GMT+9:30)","Australia/Sydney (GMT+10)","Australia/Melbourne (GMT+10)","Australia/Brisbane (GMT+10)","Australia/Hobart (GMT+10)","Pacific/Guam (GMT+10)","Pacific/Port_Moresby (GMT+10)","Asia/Vladivostok (GMT+10)","Pacific/Noumea (GMT+11)","Pacific/Norfolk (GMT+11)","Asia/Magadan (GMT+11)","Pacific/Auckland (GMT+12)","Pacific/Fiji (GMT+12)","Pacific/Chatham (GMT+12:45)","Pacific/Tongatapu (GMT+13)","Pacific/Apia (GMT+13)","Pacific/Kiritimati (GMT+14)"];
  var TIMEZONE_LABELS = {"Pacific/Midway (GMT-11)":"Pacific/Midway (GMT-11)","Pacific/Pago_Pago (GMT-11)":"Pacific/Pago_Pago (GMT-11)","Pacific/Honolulu (GMT-10)":"Pacific/Honolulu (GMT-10)","America/Adak (GMT-10)":"America/Adak (GMT-10; daylight GMT-9)","America/Anchorage (GMT-9)":"America/Anchorage (GMT-9; daylight GMT-8)","America/Juneau (GMT-9)":"America/Juneau (GMT-9; daylight GMT-8)","America/Los_Angeles (GMT-8)":"America/Los_Angeles (GMT-8; daylight GMT-7)","America/Vancouver (GMT-8)":"America/Vancouver (GMT-8; active GMT-7)","America/Tijuana (GMT-8)":"America/Tijuana (GMT-8; daylight GMT-7)","America/Denver (GMT-7)":"America/Denver (GMT-7; daylight GMT-6)","America/Phoenix (GMT-7)":"America/Phoenix (GMT-7)","America/Edmonton (GMT-7)":"America/Edmonton (GMT-7; daylight GMT-6)","America/Boise (GMT-7)":"America/Boise (GMT-7; daylight GMT-6)","America/Chicago (GMT-6)":"America/Chicago (GMT-6; daylight GMT-5)","America/Mexico_City (GMT-6)":"America/Mexico_City (GMT-6)","America/Winnipeg (GMT-6)":"America/Winnipeg (GMT-6; daylight GMT-5)","America/Guatemala (GMT-6)":"America/Guatemala (GMT-6)","America/Costa_Rica (GMT-6)":"America/Costa_Rica (GMT-6)","America/New_York (GMT-5)":"America/New_York (GMT-5; daylight GMT-4)","America/Toronto (GMT-5)":"America/Toronto (GMT-5; daylight GMT-4)","America/Detroit (GMT-5)":"America/Detroit (GMT-5; daylight GMT-4)","America/Havana (GMT-5)":"America/Havana (GMT-5; daylight GMT-4)","America/Bogota (GMT-5)":"America/Bogota (GMT-5)","America/Lima (GMT-5)":"America/Lima (GMT-5)","America/Jamaica (GMT-5)":"America/Jamaica (GMT-5)","America/Panama (GMT-5)":"America/Panama (GMT-5)","America/Halifax (GMT-4)":"America/Halifax (GMT-4; daylight GMT-3)","America/Caracas (GMT-4)":"America/Caracas (GMT-4)","America/Santiago (GMT-4)":"America/Santiago (GMT-4; daylight GMT-3)","America/La_Paz (GMT-4)":"America/La_Paz (GMT-4)","America/Manaus (GMT-4)":"America/Manaus (GMT-4)","America/Barbados (GMT-4)":"America/Barbados (GMT-4)","America/Puerto_Rico (GMT-4)":"America/Puerto_Rico (GMT-4)","America/Santo_Domingo (GMT-4)":"America/Santo_Domingo (GMT-4)","America/St_Johns (GMT-3:30)":"America/St_Johns (GMT-3:30; daylight GMT-2:30)","America/Sao_Paulo (GMT-3)":"America/Sao_Paulo (GMT-3)","America/Argentina/Buenos_Aires (GMT-3)":"America/Argentina/Buenos_Aires (GMT-3)","America/Montevideo (GMT-3)":"America/Montevideo (GMT-3)","America/Paramaribo (GMT-3)":"America/Paramaribo (GMT-3)","Atlantic/South_Georgia (GMT-2)":"Atlantic/South_Georgia (GMT-2)","Atlantic/Azores (GMT-1)":"Atlantic/Azores (GMT-1; daylight GMT+0)","Atlantic/Cape_Verde (GMT-1)":"Atlantic/Cape_Verde (GMT-1)","UTC (GMT+0)":"UTC (GMT+0)","Europe/London (GMT+0)":"Europe/London (GMT+0; daylight GMT+1)","Europe/Dublin (GMT+0)":"Europe/Dublin (GMT+0; daylight GMT+1)","Europe/Lisbon (GMT+0)":"Europe/Lisbon (GMT+0; daylight GMT+1)","Africa/Casablanca (GMT+1)":"Africa/Casablanca (GMT+1)","Africa/Accra (GMT+0)":"Africa/Accra (GMT+0)","Atlantic/Reykjavik (GMT+0)":"Atlantic/Reykjavik (GMT+0)","Europe/Paris (GMT+1)":"Europe/Paris (GMT+1; daylight GMT+2)","Europe/Berlin (GMT+1)":"Europe/Berlin (GMT+1; daylight GMT+2)","Europe/Rome (GMT+1)":"Europe/Rome (GMT+1; daylight GMT+2)","Europe/Madrid (GMT+1)":"Europe/Madrid (GMT+1; daylight GMT+2)","Europe/Amsterdam (GMT+1)":"Europe/Amsterdam (GMT+1; daylight GMT+2)","Europe/Brussels (GMT+1)":"Europe/Brussels (GMT+1; daylight GMT+2)","Europe/Vienna (GMT+1)":"Europe/Vienna (GMT+1; daylight GMT+2)","Europe/Zurich (GMT+1)":"Europe/Zurich (GMT+1; daylight GMT+2)","Europe/Stockholm (GMT+1)":"Europe/Stockholm (GMT+1; daylight GMT+2)","Europe/Oslo (GMT+1)":"Europe/Oslo (GMT+1; daylight GMT+2)","Europe/Copenhagen (GMT+1)":"Europe/Copenhagen (GMT+1; daylight GMT+2)","Europe/Warsaw (GMT+1)":"Europe/Warsaw (GMT+1; daylight GMT+2)","Europe/Prague (GMT+1)":"Europe/Prague (GMT+1; daylight GMT+2)","Europe/Budapest (GMT+1)":"Europe/Budapest (GMT+1; daylight GMT+2)","Europe/Belgrade (GMT+1)":"Europe/Belgrade (GMT+1; daylight GMT+2)","Africa/Lagos (GMT+1)":"Africa/Lagos (GMT+1)","Africa/Tunis (GMT+1)":"Africa/Tunis (GMT+1)","Africa/Cairo (GMT+2)":"Africa/Cairo (GMT+2; daylight GMT+3)","Europe/Athens (GMT+2)":"Europe/Athens (GMT+2; daylight GMT+3)","Europe/Bucharest (GMT+2)":"Europe/Bucharest (GMT+2; daylight GMT+3)","Europe/Helsinki (GMT+2)":"Europe/Helsinki (GMT+2; daylight GMT+3)","Europe/Kyiv (GMT+2)":"Europe/Kyiv (GMT+2; daylight GMT+3)","Europe/Istanbul (GMT+3)":"Europe/Istanbul (GMT+3)","Africa/Johannesburg (GMT+2)":"Africa/Johannesburg (GMT+2)","Africa/Nairobi (GMT+3)":"Africa/Nairobi (GMT+3)","Asia/Jerusalem (GMT+2)":"Asia/Jerusalem (GMT+2; daylight GMT+3)","Asia/Amman (GMT+3)":"Asia/Amman (GMT+3)","Asia/Beirut (GMT+2)":"Asia/Beirut (GMT+2; daylight GMT+3)","Europe/Moscow (GMT+3)":"Europe/Moscow (GMT+3)","Asia/Baghdad (GMT+3)":"Asia/Baghdad (GMT+3)","Asia/Riyadh (GMT+3)":"Asia/Riyadh (GMT+3)","Asia/Kuwait (GMT+3)":"Asia/Kuwait (GMT+3)","Asia/Qatar (GMT+3)":"Asia/Qatar (GMT+3)","Africa/Addis_Ababa (GMT+3)":"Africa/Addis_Ababa (GMT+3)","Asia/Tehran (GMT+3:30)":"Asia/Tehran (GMT+3:30)","Asia/Dubai (GMT+4)":"Asia/Dubai (GMT+4)","Asia/Muscat (GMT+4)":"Asia/Muscat (GMT+4)","Asia/Baku (GMT+4)":"Asia/Baku (GMT+4)","Asia/Tbilisi (GMT+4)":"Asia/Tbilisi (GMT+4)","Indian/Mauritius (GMT+4)":"Indian/Mauritius (GMT+4)","Asia/Kabul (GMT+4:30)":"Asia/Kabul (GMT+4:30)","Asia/Karachi (GMT+5)":"Asia/Karachi (GMT+5)","Asia/Tashkent (GMT+5)":"Asia/Tashkent (GMT+5)","Asia/Yekaterinburg (GMT+5)":"Asia/Yekaterinburg (GMT+5)","Asia/Kolkata (GMT+5:30)":"Asia/Kolkata (GMT+5:30)","Asia/Colombo (GMT+5:30)":"Asia/Colombo (GMT+5:30)","Asia/Kathmandu (GMT+5:45)":"Asia/Kathmandu (GMT+5:45)","Asia/Dhaka (GMT+6)":"Asia/Dhaka (GMT+6)","Asia/Almaty (GMT+5)":"Asia/Almaty (GMT+5)","Asia/Rangoon (GMT+6:30)":"Asia/Rangoon (GMT+6:30)","Asia/Bangkok (GMT+7)":"Asia/Bangkok (GMT+7)","Asia/Jakarta (GMT+7)":"Asia/Jakarta (GMT+7)","Asia/Ho_Chi_Minh (GMT+7)":"Asia/Ho_Chi_Minh (GMT+7)","Asia/Singapore (GMT+8)":"Asia/Singapore (GMT+8)","Asia/Kuala_Lumpur (GMT+8)":"Asia/Kuala_Lumpur (GMT+8)","Asia/Shanghai (GMT+8)":"Asia/Shanghai (GMT+8)","Asia/Hong_Kong (GMT+8)":"Asia/Hong_Kong (GMT+8)","Asia/Taipei (GMT+8)":"Asia/Taipei (GMT+8)","Asia/Manila (GMT+8)":"Asia/Manila (GMT+8)","Australia/Perth (GMT+8)":"Australia/Perth (GMT+8)","Asia/Tokyo (GMT+9)":"Asia/Tokyo (GMT+9)","Asia/Seoul (GMT+9)":"Asia/Seoul (GMT+9)","Asia/Pyongyang (GMT+9)":"Asia/Pyongyang (GMT+9)","Australia/Adelaide (GMT+9:30)":"Australia/Adelaide (GMT+9:30; daylight GMT+10:30)","Australia/Darwin (GMT+9:30)":"Australia/Darwin (GMT+9:30)","Australia/Sydney (GMT+10)":"Australia/Sydney (GMT+10; daylight GMT+11)","Australia/Melbourne (GMT+10)":"Australia/Melbourne (GMT+10; daylight GMT+11)","Australia/Brisbane (GMT+10)":"Australia/Brisbane (GMT+10)","Australia/Hobart (GMT+10)":"Australia/Hobart (GMT+10; daylight GMT+11)","Pacific/Guam (GMT+10)":"Pacific/Guam (GMT+10)","Pacific/Port_Moresby (GMT+10)":"Pacific/Port_Moresby (GMT+10)","Asia/Vladivostok (GMT+10)":"Asia/Vladivostok (GMT+10)","Pacific/Noumea (GMT+11)":"Pacific/Noumea (GMT+11)","Pacific/Norfolk (GMT+11)":"Pacific/Norfolk (GMT+11; daylight GMT+12)","Asia/Magadan (GMT+11)":"Asia/Magadan (GMT+11)","Pacific/Auckland (GMT+12)":"Pacific/Auckland (GMT+12; daylight GMT+13)","Pacific/Fiji (GMT+12)":"Pacific/Fiji (GMT+12)","Pacific/Chatham (GMT+12:45)":"Pacific/Chatham (GMT+12:45; daylight GMT+13:45)","Pacific/Tongatapu (GMT+13)":"Pacific/Tongatapu (GMT+13)","Pacific/Apia (GMT+13)":"Pacific/Apia (GMT+13)","Pacific/Kiritimati (GMT+14)":"Pacific/Kiritimati (GMT+14)"};
  var PRODUCT_SETTINGS = {"photo_source":{"entity":"select/Photos: Source","domain":"select","default":"All Photos","options":["All Photos","Favorites","Album","Person","Tag","Memories"]},"album_order":{"entity":"select/Photos: Album Order","domain":"select","default":"Random albums","options":["Random albums","Album list order"]},"date_filter_mode":{"entity":"select/Photos: Date Filter Mode","domain":"select","default":"Fixed Range","options":["Fixed Range","Relative Range"]},"relative_unit":{"entity":"select/Photos: Relative Unit","domain":"select","default":"Years","options":["Months","Years"]},"photo_orientation":{"entity":"select/Photos: Orientation","domain":"select","default":"Any","options":["Any","Portrait Only","Landscape Only"]},"display_mode":{"entity":"select/Photos: Display Mode","domain":"select","default":"Fill","options":["Fill","Fit"]},"interval":{"entity":"select/Photos: Slideshow Interval","domain":"select","default":"15 seconds","options":["10 seconds","15 seconds","20 seconds","30 seconds","45 seconds","1 minute","2 minutes","3 minutes","5 minutes","10 minutes"]},"conn_timeout":{"entity":"select/Screen: Connection Timeout","domain":"select","default":"10 minutes","options":["30 seconds","45 seconds","1 minute","2 minutes","3 minutes","5 minutes","10 minutes","15 minutes","20 minutes","30 minutes"]},"screen_rotation":{"entity":"select/Screen: Rotation","domain":"select","default":"0","options":["0","180"],"developerOptions":["90","270"]},"photo_metadata_date_format":{"entity":"select/Device: Metadata Date Format","domain":"select","default":"Date Taken","options":["Relative Date","Date Taken"]},"photo_metadata_date_taken_format":{"entity":"select/Device: Metadata Date Taken Format","domain":"select","default":"1 January, 2026","options":["1 January, 2026","January 1, 2026"]},"clock_format":{"entity":"select/Clock: Format","domain":"select","default":"24 Hour","options":["24 Hour","12 Hour"]},"update_frequency":{"entity":"select/Firmware: Update Frequency","domain":"select","default":"Daily","options":["Hourly","Daily","Weekly","Monthly"]},"auto_update":{"entity":"switch/Firmware: Auto Update","domain":"switch","default":true,"options":[]},"firmware_manifest_url":{"entity":"text/Firmware: Manifest URL","domain":"text","default":"","options":[]},"date_filter_enabled":{"entity":"switch/Photos: Date Filter","domain":"switch","default":false,"options":[]},"date_from":{"entity":"text/Photos: Date From","domain":"text","default":"","options":[]},"date_to":{"entity":"text/Photos: Date To","domain":"text","default":"","options":[]},"relative_amount":{"entity":"number/Photos: Relative Amount","domain":"number","default":1,"options":[],"min":1,"max":120,"step":1},"schedule_enabled":{"entity":"switch/Screen: Schedule Enabled","domain":"switch","default":false,"options":[]},"schedule_on_hour":{"entity":"number/Screen: Schedule On Hour","domain":"number","default":6,"options":[],"min":0,"max":23,"step":1},"schedule_off_hour":{"entity":"number/Screen: Schedule Off Hour","domain":"number","default":23,"options":[],"min":0,"max":23,"step":1},"schedule_wake_timeout":{"entity":"number/Screen: Schedule Wake Timeout","domain":"number","default":60,"options":[],"min":10,"max":3600,"step":10},"brightness_day":{"entity":"number/Screen: Daytime Brightness","domain":"number","default":100,"options":[],"min":10,"max":100,"step":5},"brightness_night":{"entity":"number/Screen: Nighttime Brightness","domain":"number","default":75,"options":[],"min":10,"max":100,"step":5},"base_tone_enabled":{"entity":"switch/Screen: Tone Adjustment","domain":"switch","default":false,"options":[]},"base_tone":{"entity":"number/Screen: Display Tone","domain":"number","default":0,"options":[],"min":0,"max":100,"step":5},"warm_tones_enabled":{"entity":"switch/Screen: Night Tone Adjustment","domain":"switch","default":false,"options":[]},"warm_tone_intensity":{"entity":"number/Screen: Warm Tone Intensity","domain":"number","default":50,"options":[],"min":10,"max":100,"step":5},"warm_tone_override":{"entity":"switch/Screen: Warm Tone Override","domain":"switch","default":false,"options":[]},"portrait_pairing":{"entity":"switch/Photos: Portrait Pairing","domain":"switch","default":true,"options":[]},"photo_metadata_date_enabled":{"entity":"switch/Device: Metadata Date","domain":"switch","default":true,"options":[]},"photo_metadata_location_enabled":{"entity":"switch/Device: Metadata Location","domain":"switch","default":true,"options":[]}};
  var STATIC_ENTITIES = {"firmware":{"entity":"text_sensor/Firmware: Version"},"timezone":{"entity":"select/Clock: Timezone","optionsKey":"tz_options","default":"Europe/London (GMT+0)"},"ntp_server_1":{"entity":"text/Clock: NTP Server 1","default":"0.pool.ntp.org"},"ntp_server_2":{"entity":"text/Clock: NTP Server 2","default":"1.pool.ntp.org"},"ntp_server_3":{"entity":"text/Clock: NTP Server 3","default":"2.pool.ntp.org"},"album_ids":{"entity":"text/Photos: Album IDs"},"album_labels":{"entity":"text/Photos: Album Labels"},"person_ids":{"entity":"text/Photos: Person IDs"},"person_labels":{"entity":"text/Photos: Person Labels"},"tag_ids":{"entity":"text/Photos: Tag IDs"},"tag_labels":{"entity":"text/Photos: Tag Labels"},"sunrise":{"entity":"text_sensor/Screen: Sunrise"},"sunset":{"entity":"text_sensor/Screen: Sunset"},"developer_features_enabled":{"entity":"switch/Developer: Features","boolFromState":true},"show_clock":{"entity":"switch/Clock: Show","boolFromState":true,"default":true},"c6_current_firmware":{"entity":"text_sensor/ESP32-C6: Current Firmware","default":"Unknown"},"c6_available_firmware":{"entity":"text_sensor/ESP32-C6: Available Firmware","default":"Unknown"}};
  var MANUAL_ENTITIES = {"immich_url":{"entity":"text/Connection: Server URL"},"api_key":{"entity":"text/Connection: API Key"},"backlight":{"entity":"light/Screen: Backlight"},"update":{"entity":"update/Firmware: Update"},"apply_photo_source":{"entity":"button/Apply Photo Source"},"firmware_check":{"entity":"button/Firmware: Check for Update"},"c6_firmware_check":{"entity":"button/Firmware ESP32-C6: Check for Update"},"c6_firmware_install":{"entity":"button/Firmware ESP32-C6: Install Update"},"reboot_screen":{"entity":"button/Device: Reboot Screen"}};
  var MANUAL_STATE_KEYS = ["immich_url","api_key"];
  var ENTITY_ALIASES = {"schedule_enabled":[{"entity":"switch/Screen: Schedule","boolFromState":true}],"schedule_on_hour":[{"entity":"number/Screen: Schedule On","default":6,"number":true}],"schedule_off_hour":[{"entity":"number/Screen: Schedule Off","default":23,"number":true}]};
  var BACKUP_CONFIG_VERSION = 1;
  var BACKUP_SCHEMA = [{"group":"connection","field":"immich_url","state_keys":["immich_url"]},{"group":"connection","field":"api_key","state_keys":["api_key"]},{"group":"photos","field":"source","state_keys":["photo_source"]},{"group":"photos","field":"album_order","state_keys":["album_order"]},{"group":"photos","field":"album_ids","state_keys":["album_ids"]},{"group":"photos","field":"album_labels","state_keys":["album_labels"]},{"group":"photos","field":"person_ids","state_keys":["person_ids"]},{"group":"photos","field":"person_labels","state_keys":["person_labels"]},{"group":"photos","field":"tag_ids","state_keys":["tag_ids"]},{"group":"photos","field":"tag_labels","state_keys":["tag_labels"]},{"group":"photos","field":"date_filter_enabled","state_keys":["date_filter_enabled"]},{"group":"photos","field":"date_filter_mode","state_keys":["date_filter_mode"]},{"group":"photos","field":"date_from","state_keys":["date_from"]},{"group":"photos","field":"date_to","state_keys":["date_to"]},{"group":"photos","field":"relative_amount","state_keys":["relative_amount"]},{"group":"photos","field":"relative_unit","state_keys":["relative_unit"]},{"group":"photos","field":"orientation","state_keys":["photo_orientation"]},{"group":"photos","field":"portrait_pairing","state_keys":["portrait_pairing"]},{"group":"photos","field":"display_mode","state_keys":["display_mode"]},{"group":"frequency","field":"interval","state_keys":["interval"]},{"group":"frequency","field":"conn_timeout","state_keys":["conn_timeout"]},{"group":"firmware_updates","field":"auto_update","state_keys":["auto_update"]},{"group":"firmware_updates","field":"update_frequency","state_keys":["update_frequency"]},{"group":"firmware_updates","field":"manifest_url","state_keys":["firmware_manifest_url"]},{"group":"clock","field":"show","state_keys":["show_clock"]},{"group":"clock","field":"format","state_keys":["clock_format"]},{"group":"clock","field":"timezone","state_keys":["timezone"]},{"group":"clock","field":"ntp_servers","state_keys":["ntp_server_1","ntp_server_2","ntp_server_3"]},{"group":"screen","field":"brightness_day","state_keys":["brightness_day"]},{"group":"screen","field":"brightness_night","state_keys":["brightness_night"]},{"group":"screen","field":"schedule_enabled","state_keys":["schedule_enabled"]},{"group":"screen","field":"schedule_on_hour","state_keys":["schedule_on_hour"]},{"group":"screen","field":"schedule_off_hour","state_keys":["schedule_off_hour"]},{"group":"screen","field":"schedule_wake_timeout","state_keys":["schedule_wake_timeout"]},{"group":"screen","field":"base_tone_enabled","state_keys":["base_tone_enabled"]},{"group":"screen","field":"base_tone","state_keys":["base_tone"]},{"group":"screen","field":"warm_tones_enabled","state_keys":["warm_tones_enabled"]},{"group":"screen","field":"warm_tone_intensity","state_keys":["warm_tone_intensity"]},{"group":"screen","field":"warm_tone_override","state_keys":["warm_tone_override"]},{"group":"screen","field":"rotation","state_keys":["screen_rotation"]}];
  var LIVE_RENDER_STATE_KEYS = ["screen_rotation","portrait_pairing","developer_features_enabled"];
  var LIVE_RENDER_STATE_PREFIXES = ["photo_metadata_","schedule_"];
  var FIRMWARE_MANIFEST_URLS = {"stable":"https://jtenniswood.github.io/espframe/firmware/manifest.json"};
  var DOCS_BASE_URL = "https://jtenniswood.github.io/espframe";
  var WEB_UI_TABS = [{"id":"immich","label":"Immich"},{"id":"settings","label":"Device"},{"id":"logs","label":"Logs"}];
  var WEB_UI_CARDS = [{"id":"connection","label":"Connection","tab":"immich","function":"makeConnectionCard","settings":["conn_timeout"],"staticEntities":[],"manualEntities":["immich_url","api_key"]},{"id":"frequency","label":"Frequency","tab":"immich","function":"makeFrequencyCard","settings":["interval"],"staticEntities":[],"manualEntities":[]},{"id":"photo_source","label":"Photo Source","tab":"immich","function":"makePhotoSourceCard","settings":["photo_source","album_order"],"staticEntities":["album_ids","album_labels","person_ids","person_labels","tag_ids","tag_labels"],"manualEntities":["apply_photo_source"]},{"id":"advanced_filters","label":"Advanced Filters","tab":"immich","function":"makeAdvancedFiltersCard","settings":["date_filter_enabled","date_filter_mode","date_from","date_to","relative_amount","relative_unit"],"staticEntities":[],"manualEntities":["apply_photo_source"]},{"id":"layout","label":"Layout","tab":"immich","function":"makeLayoutCard","settings":["portrait_pairing","photo_orientation","display_mode"],"staticEntities":[],"manualEntities":[]},{"id":"metadata","label":"Metadata","tab":"immich","function":"makeMetadataCard","settings":["photo_metadata_date_enabled","photo_metadata_location_enabled","photo_metadata_date_format","photo_metadata_date_taken_format"],"staticEntities":[],"manualEntities":[]},{"id":"screen_brightness","label":"Screen Brightness","tab":"settings","function":"makeScreenBrightnessCard","settings":["brightness_day","brightness_night"],"staticEntities":["sunrise","sunset"],"manualEntities":[]},{"id":"screen_tone","label":"Screen Tone","tab":"settings","function":"makeScreenToneCard","settings":["base_tone_enabled","base_tone","warm_tones_enabled","warm_tone_intensity","warm_tone_override"],"staticEntities":[],"manualEntities":[]},{"id":"night_schedule","label":"Night Schedule","tab":"settings","function":"makeNightScheduleCard","settings":["schedule_enabled","schedule_on_hour","schedule_off_hour","schedule_wake_timeout"],"staticEntities":["sunrise","sunset"],"manualEntities":[]},{"id":"rotation","label":"Rotation","tab":"settings","function":"makeRotationCard","settings":["screen_rotation"],"staticEntities":["developer_features_enabled"],"manualEntities":[]},{"id":"clock","label":"Clock","tab":"settings","function":"makeClockCard","settings":["clock_format"],"staticEntities":["show_clock","timezone","ntp_server_1","ntp_server_2","ntp_server_3"],"manualEntities":[]},{"id":"firmware","label":"Firmware","tab":"settings","function":"makeFirmwareCard","settings":["update_frequency","auto_update","firmware_manifest_url"],"staticEntities":["firmware"],"manualEntities":["update","firmware_check","reboot_screen"]},{"id":"wifi","label":"WiFi","tab":"settings","function":"makeWifiCard","settings":[],"staticEntities":["c6_current_firmware","c6_available_firmware"],"manualEntities":["c6_firmware_check","c6_firmware_install"]},{"id":"developer","label":"Developer","tab":"settings","function":"makeDeveloperCard","settings":[],"staticEntities":["developer_features_enabled"],"manualEntities":[]},{"id":"backup","label":"Backup","tab":"settings","function":"makeBackupCard","settings":[],"staticEntities":[],"manualEntities":[]}];
  var WEB_UI_LOGS_RETAINED_LINES = 1000;
  var SUPPORT_URL = "https://www.buymeacoffee.com/jtenniswood";
  var SUPPORT_BUTTON_IMAGE_URL = "https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png";

  var S = {
    tz_options: TIMEZONES,
    tz_labels: TIMEZONE_LABELS,
    brightness: 100,
    backlight_on: true,
    immich_url: "",
    api_key: "",
    firmware: "",
    installed_version: "",
    latest_version: "",
    update_available: false,
    brightness_current: 0,
    sunrise: "",
    sunset: "",
    album_ids: "",
    album_labels: "",
    person_ids: "",
    person_labels: "",
    tag_ids: "",
    tag_labels: "",
    developer_features_enabled: false,
  };

  function registerStaticEntityStateDefaults() {
    if (!STATIC_ENTITIES) return;
    Object.keys(STATIC_ENTITIES).forEach(function (key) {
      var spec = STATIC_ENTITIES[key];
      if (!spec || spec.default === undefined) return;
      if (S[key] === undefined) S[key] = spec.default;
    });
  }

  function registerProductSettingStateDefaults() {
    if (!PRODUCT_SETTINGS) return;
    Object.keys(PRODUCT_SETTINGS).forEach(function (key) {
      var spec = PRODUCT_SETTINGS[key];
      if (!spec) return;
      if (S[key] === undefined) S[key] = spec.default !== undefined ? spec.default : "";
    });
  }

  registerStaticEntityStateDefaults();
  registerProductSettingStateDefaults();

  function productNumberSettingField(key, field, fallback) {
    var spec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    var value = spec && spec[field] !== undefined ? Number(spec[field]) : NaN;
    return isFinite(value) ? value : fallback;
  }

  function productNumberMin(key, fallback) {
    return productNumberSettingField(key, "min", fallback);
  }

  function productNumberMax(key, fallback) {
    return productNumberSettingField(key, "max", fallback);
  }

  function productNumberStep(key, fallback) {
    return productNumberSettingField(key, "step", fallback);
  }

  function productSettingOptions(key, includeDeveloper) {
    var spec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    var options = spec && Array.isArray(spec.options) ? spec.options.slice() : [];
    if (includeDeveloper && spec && Array.isArray(spec.developerOptions)) {
      spec.developerOptions.forEach(function (option) {
        if (options.indexOf(option) === -1) options.push(option);
      });
    }
    return options;
  }

  var CSS = "*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}:root{--bg:#1b1b1f;--surface:#202127;--surface2:#2e2e32;--border:#3c3f44;--border-hover:rgba(255,255,255,.16);--text:#dfdfd6;--text2:#98989f;--text3:#6a6a71;--accent:#5c73e7;--accent-hover:#a8b1ff;--accent-soft:rgba(100,108,255,.16);--success:#30a46c;--success-soft:rgba(48,164,108,.14);--danger:#f14158;--radius:12px;--action-r:9999px;--gap:16px;--shadow-1:0 1px 2px rgba(0,0,0,.2),0 1px 2px rgba(0,0,0,.24);--shadow-2:0 3px 12px rgba(0,0,0,.28),0 1px 4px rgba(0,0,0,.2);--shadow-3:0 12px 32px rgba(0,0,0,.35),0 2px 6px rgba(0,0,0,.24)}esp-app{display:none !important}html{font-size:16px}body{font-family:Inter,ui-sans-serif,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);line-height:1.7;min-height:100vh;margin:0;-webkit-font-smoothing:antialiased;-moz-osx-font-smoothing:grayscale}#sp-app{width:100%;max-width:960px;margin:0 auto}.sp-header{display:flex;align-items:center;background:var(--bg);border-bottom:1px solid var(--border);position:sticky;top:0;z-index:100;height:56px;padding:0 20px}.sp-brand{font-size:1rem;font-weight:600;color:var(--text);margin-right:auto;white-space:nowrap;letter-spacing:-.01em}.sp-nav{display:flex;align-items:center;height:100%}.sp-tab{padding:0 16px;height:100%;display:flex;align-items:center;color:var(--text2);cursor:pointer;font-size:.875rem;font-weight:500;border-bottom:2px solid transparent;text-decoration:none;transition:color .2s}.sp-tab:hover{color:var(--text)}.sp-tab.active{color:var(--accent);border-bottom-color:var(--accent)}.sp-tab-docs{position:relative;gap:6px;margin-left:8px;padding-left:24px}.sp-tab-docs::before{content:'';position:absolute;left:0;top:12px;bottom:12px;width:1px;background:var(--border)}.sp-docs-icon{font-size:16px;line-height:1;opacity:.7}.sp-page{display:none}.sp-page.active{display:block}.sp-settings-wrap{padding:var(--gap)}.brand{font-size:1.6rem;font-weight:700;letter-spacing:-.02em;background:linear-gradient(135deg,var(--accent) 0%,#a78bfa 100%);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}h1{font-size:1.6rem;font-weight:700;margin-bottom:4px;letter-spacing:-.02em}h2{font-size:1rem;font-weight:500;margin-bottom:20px;color:var(--text2);letter-spacing:.01em}.subtitle{font-size:.9rem;color:var(--text2);margin-bottom:24px;line-height:1.6}.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:24px;margin-bottom:var(--gap);transition:border-color .25s}.card:hover{border-color:#4a4d54}.card h3{font-size:.875rem;font-weight:600;margin-bottom:14px;color:var(--text);letter-spacing:-.01em}.card-header{display:flex;justify-content:space-between;align-items:center;cursor:pointer;user-select:none;margin:-24px -24px 0 -24px;padding:24px 24px 0 24px}.card-header h3{margin:0}.card-body{padding-top:20px}.card-chevron{display:inline-flex;align-items:center;justify-content:center;width:24px;height:24px;color:var(--text3);transition:transform .25s ease;flex-shrink:0}.card-chevron svg{width:100%;height:100%}.card.collapsed .card-chevron{transform:rotate(-90deg)}.card.collapsed .card-body{display:none}.card-header-right{display:flex;align-items:center;gap:8px}.on-badge{display:none;align-items:center;gap:4px;font-size:.6rem;font-weight:600;color:var(--success);padding:2px 8px 2px 6px;background:var(--success-soft);border-radius:999px;text-transform:uppercase;letter-spacing:.06em;white-space:nowrap}.card.collapsed .on-badge.active{display:inline-flex}.on-badge::before{content:'';display:block;width:6px;height:6px;border-radius:50%;background:var(--success);flex-shrink:0}.field{margin-bottom:22px}.field:last-child{margin-bottom:0}label{display:block;font-size:.85rem;color:var(--text2);margin-bottom:6px;font-weight:500}.filter-relative-row{display:grid;grid-template-columns:minmax(84px,104px) minmax(0,1fr);gap:12px}.filter-relative-row .field{margin-bottom:0}input[type='text'],input[type='password'],input[type='url'],input[type='date'],input[type='number']{width:100%;padding:10px 14px;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.9rem;letter-spacing:0;outline:none;transition:border-color .25s,box-shadow .25s;font-family:inherit;font-variant-numeric:tabular-nums;color-scheme:dark}input[type='text']:focus,input[type='password']:focus,input[type='url']:focus,input[type='date']:focus,input[type='number']:focus{border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}input[type='date']::-webkit-datetime-edit,input[type='date']::-webkit-date-and-time-value{color:var(--text);font:inherit;letter-spacing:0;text-align:left}input[type='date']::-webkit-datetime-edit-fields-wrapper,input[type='date']::-webkit-datetime-edit-text,input[type='date']::-webkit-datetime-edit-day-field,input[type='date']::-webkit-datetime-edit-month-field,input[type='date']::-webkit-datetime-edit-year-field{font:inherit;color:inherit;letter-spacing:0}input[type='date']::-webkit-calendar-picker-indicator{filter:invert(.7);cursor:pointer}input::placeholder{color:var(--text2);opacity:.7}.select,select{width:100%;padding:10px 14px;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:.9rem;outline:none;transition:border-color .25s,box-shadow .25s;-webkit-appearance:none;appearance:none;color-scheme:dark;font-family:inherit;background-image:url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23888' d='M6 8L1 3h10z'/%3E%3C/svg%3E\");background-repeat:no-repeat;background-position:right 14px center;padding-right:36px}.select:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}select option{background:var(--surface);color:var(--text)}.input-group{display:flex;gap:8px}.input-group input{flex:1}.photo-id-list{display:flex;flex-direction:column;gap:8px}.photo-id-row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;align-items:start}.photo-id-fields{display:grid;grid-template-columns:minmax(220px,2fr) minmax(160px,1fr);gap:8px}.photo-id-row-actions{display:flex;gap:6px}.photo-id-actions{display:flex;justify-content:flex-start;margin-top:18px}.btn.btn-icon{width:40px;height:40px;padding:0;display:inline-flex;align-items:center;justify-content:center;border-radius:20px;font-size:1.2rem;line-height:1;flex-shrink:0}.btn{padding:10px 20px;border:none;border-radius:20px;font-size:.875rem;font-weight:600;cursor:pointer;transition:background .25s,opacity .25s,box-shadow .25s;font-family:inherit;letter-spacing:.01em}.btn:active{opacity:.85}.btn-primary{background:var(--accent);color:#fff}.btn-primary:hover{background:var(--accent-hover);box-shadow:0 2px 12px var(--accent-soft)}.btn-secondary{background:var(--surface2);color:var(--text);border:1px solid var(--border)}.btn-secondary:hover{border-color:var(--border-hover);background:rgba(255,255,255,.06)}.btn-sm{padding:7px 14px;font-size:.8rem}.btn-block{width:100%;display:block}.btn:disabled{opacity:.35;cursor:not-allowed}.field-error{font-size:.75rem;color:var(--danger);margin-top:4px}.field-error:empty{display:none}.toggle-row{display:flex;justify-content:space-between;align-items:center;min-height:36px}.toggle-row span{font-size:.9rem}.toggle{position:relative;width:44px;height:24px;background:var(--surface2);border-radius:999px;cursor:pointer;transition:background .25s;border:1px solid var(--border)}.toggle.on{background:var(--accent);border-color:var(--accent)}.toggle::after{content:'';position:absolute;top:2px;left:2px;width:18px;height:18px;border-radius:50%;background:#fff;transition:transform .25s ease;box-shadow:0 1px 3px rgba(0,0,0,.3)}.toggle.on::after{transform:translateX(20px)}.segment{display:flex;border-radius:8px;overflow:hidden;border:1px solid var(--border)}.segment button{flex:1;padding:8px 0;background:var(--surface2);color:var(--text2);border:none;font-size:.85rem;cursor:pointer;transition:background .25s,color .25s;font-family:inherit}.segment button.active{background:var(--accent);color:#fff}.range-wrap{display:flex;align-items:center;gap:12px}.range-wrap input[type='range']{flex:1;-webkit-appearance:none;height:4px;background:var(--surface2);border-radius:2px;outline:none}.range-wrap input[type='range']::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;background:var(--accent);cursor:pointer;box-shadow:0 0 0 3px var(--accent-soft);transition:box-shadow .2s}.range-wrap input[type='range']::-webkit-slider-thumb:hover{box-shadow:0 0 0 5px var(--accent-soft)}.range-wrap input[type='range']::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:var(--accent);cursor:pointer;border:none}.range-val{min-width:42px;text-align:right;font-size:.85rem;color:var(--text2);font-variant-numeric:tabular-nums}.range-label{font-size:.85rem;color:var(--text2);white-space:nowrap}.status{display:inline-flex;align-items:center;gap:6px;font-size:.8rem;color:var(--text2);margin-top:4px}.dot{width:8px;height:8px;border-radius:50%;flex-shrink:0}.dot.green{background:var(--success)}.dot.red{background:var(--danger)}.dot.orange{background:#ff9800}.wizard-steps{display:flex;gap:8px;margin-bottom:24px}.wizard-steps .step{flex:1;height:3px;border-radius:2px;background:var(--surface2);transition:background .3s}.wizard-steps .step.active{background:var(--accent)}.wizard-steps .step.done{background:var(--success)}.wizard-nav{display:flex;gap:8px;margin-top:20px}.wizard-nav .btn{flex:1}.fade-in{animation:fadeIn .35s ease}@keyframes fadeIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}.sun-info{font-size:.8rem;color:var(--text2);padding:10px 14px;background:var(--surface2);border-radius:8px;text-align:center;border:1px solid var(--border)}.version{text-align:center;font-size:.75rem;color:var(--text2);margin-top:8px;opacity:.5}.fw-body{display:flex;flex-direction:column;gap:12px}.fw-body .field{margin-bottom:0}.fw-updates{display:flex;flex-direction:column;gap:12px}.fw-row{display:flex;align-items:center;justify-content:space-between;min-height:36px}.fw-label{font-size:.9rem}.fw-status{font-size:.8rem;color:var(--text2)}.field-hint{font-size:.75rem;color:var(--text2);margin-top:6px;margin-bottom:8px}.key-mask{flex:1;padding:10px 14px;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text2);font-size:.9rem;letter-spacing:2px}.check-wrap{display:flex;align-items:center;gap:8px;flex-shrink:0}.sp-log-toolbar{display:flex;justify-content:flex-end;padding:12px var(--gap) 0}.sp-log-clear{background:var(--surface2);color:var(--text);border:1px solid var(--border);border-radius:8px;padding:8px 14px;font-size:.8rem;font-weight:500;cursor:pointer;font-family:inherit;transition:all .25s}.sp-log-clear:hover{background:var(--border);border-color:#4a4d54}.sp-log-output{margin:8px var(--gap) var(--gap);padding:16px;background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);font-family:ui-monospace,'SF Mono',SFMono-Regular,Menlo,Consolas,monospace;font-size:.75rem;line-height:1.7;color:var(--text2);overflow-x:auto;overflow-y:auto;max-height:70vh;white-space:pre;word-break:break-all}.sp-log-line{padding:1px 0;border-left:3px solid transparent;padding-left:8px}.sp-log-error{color:#f66f81;border-left-color:#f14158;background:rgba(244,63,94,.08)}.sp-log-warn{color:#f9b44e;border-left-color:#da8b17;background:rgba(234,179,8,.06)}.sp-log-info{color:#3dd68c}.sp-log-config{color:#c8abfa}.sp-log-debug{color:#5c73e7}.sp-log-verbose{color:var(--text2)}.banner{position:fixed;top:16px;left:50%;transform:translateX(-50%);z-index:9999;padding:10px 24px;border-radius:var(--radius);font-size:.85rem;font-weight:600;color:#fff;box-shadow:var(--shadow-2);animation:bannerIn .25s ease;max-width:calc(100% - 32px);text-align:center}.banner-success{background:var(--success)}.banner-error{background:var(--danger)}@keyframes bannerIn{from{opacity:0;transform:translateX(-50%) translateY(-12px)}to{opacity:1;transform:translateX(-50%) translateY(0)}}.backup-row{display:flex;gap:8px}.backup-row .btn{flex:1}.sp-support-btn{position:fixed;right:28px;bottom:28px;z-index:150;display:inline-flex;align-items:center;justify-content:center;width:217px;height:60px;overflow:hidden;border-radius:999px;background:#ffdd00;color:#000;font-family:Arial,sans-serif;font-size:18px;font-weight:700;line-height:1;text-decoration:none;box-shadow:0 2px 5px rgba(0,0,0,.15)}.sp-support-btn span{position:absolute}.sp-support-btn img{position:absolute;inset:0;width:217px;height:60px;display:block;border-radius:999px}@media(max-width:768px){.sp-header{padding:0 12px;height:48px}.sp-brand{font-size:.875rem}.sp-tab{padding:0 12px;font-size:.8rem}.photo-id-row{grid-template-columns:1fr}.photo-id-fields{grid-template-columns:1fr}.photo-id-row-actions{justify-content:flex-end}}@media(max-width:480px){.sp-header{padding:0 10px}.sp-tab{padding:0 10px;font-size:.75rem}.sp-tab-docs{gap:4px}}.mb-8{margin-bottom:8px}.mb-12{margin-bottom:12px}.mb-20{margin-bottom:20px}.mb-24{margin-bottom:24px}.mt-12{margin-top:12px}";
  var FAVICON_SVG = '<svg xmlns="http://www.w3.org/2000/svg" id="mdi-home-automation" viewBox="0 0 24 24"><path fill="#5c73e7" d="M12,3L2,12H5V20H19V12H22L12,3M12,8.5C14.34,8.5 16.46,9.43 18,10.94L16.8,12.12C15.58,10.91 13.88,10.17 12,10.17C10.12,10.17 8.42,10.91 7.2,12.12L6,10.94C7.54,9.43 9.66,8.5 12,8.5M12,11.83C13.4,11.83 14.67,12.39 15.6,13.3L14.4,14.47C13.79,13.87 12.94,13.5 12,13.5C11.06,13.5 10.21,13.87 9.6,14.47L8.4,13.3C9.33,12.39 10.6,11.83 12,11.83M12,15.17C12.94,15.17 13.7,15.91 13.7,16.83C13.7,17.75 12.94,18.5 12,18.5C11.06,18.5 10.3,17.75 10.3,16.83C10.3,15.91 11.06,15.17 12,15.17Z"/></svg>';

  var style = document.createElement("style");
  style.textContent = CSS;
  document.head.appendChild(style);
  ensureFavicon();

  var fonts = document.createElement("link");
  fonts.rel = "stylesheet";
  fonts.href = "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap";
  document.head.appendChild(fonts);

  var els = {};
  var app;

    function ensureFavicon() {
    var icon = document.querySelector('link[rel="icon"]') || document.createElement("link");
    icon.rel = "icon";
    icon.type = "image/svg+xml";
    icon.href = "data:image/svg+xml," + encodeURIComponent(FAVICON_SVG);
    if (!icon.parentNode) document.head.appendChild(icon);
  }

  function buildUI() {
    var root = document.createElement("div");
    root.id = "sp-app";

    var banner = document.createElement("div");
    banner.className = "banner";
    banner.style.display = "none";
    root.appendChild(banner);
    els.banner = banner;

    buildHeader(root);
    buildImmichPage(root);
    buildSettingsPage(root);
    buildLogsPage(root);

    var espApp = document.querySelector("esp-app");
    if (espApp) {
      espApp.parentNode.insertBefore(root, espApp);
    } else {
      document.body.insertBefore(root, document.body.firstChild);
    }
    els.root = root;
    switchTab("immich");
    addSupportButton();
  }

  function addSupportButton() {
    if (document.querySelector(".sp-support-btn")) return;
    var link = document.createElement("a");
    link.className = "sp-support-btn";
    link.href = SUPPORT_URL;
    link.target = "_blank";
    link.rel = "noopener";
    link.setAttribute("aria-label", "Buy Me A Coffee");
    link.innerHTML = '<span>Buy Me A Coffee</span><img src="' + SUPPORT_BUTTON_IMAGE_URL + '" alt="Buy Me A Coffee" height="60" style="border-radius:999px;">';
    document.body.appendChild(link);
  }

  function buildHeader(parent) {
    var header = document.createElement("div");
    header.className = "sp-header";

    var brand = document.createElement("div");
    brand.className = "sp-brand";
    brand.textContent = "EspFrame";
    header.appendChild(brand);

    var nav = document.createElement("nav");
    nav.className = "sp-nav";
    nav.setAttribute("aria-label", "Primary");

    webUiTabs().forEach(function (t) {
      var tab = document.createElement("div");
      tab.className = "sp-tab";
      tab.setAttribute("role", "tab");
      tab.setAttribute("aria-selected", "false");
      tab.textContent = t.label;
      tab.addEventListener("click", function () { switchTab(t.id); });
      nav.appendChild(tab);
      els["tab_" + t.id] = tab;
    });

    var docsLink = document.createElement("a");
    docsLink.className = "sp-tab sp-tab-docs";
    docsLink.href = DOCS_BASE_URL + "/";
    docsLink.target = "_blank";
    docsLink.rel = "noopener";
    docsLink.innerHTML = 'Docs <span class="sp-docs-icon" aria-hidden="true">&#8599;</span>';
    nav.appendChild(docsLink);

    header.appendChild(nav);
    parent.appendChild(header);
  }

  function webUiTabs() {
    return Array.isArray(WEB_UI_TABS) && WEB_UI_TABS.length
      ? WEB_UI_TABS
      : [{ id: "immich", label: "Immich" }, { id: "settings", label: "Device" }];
  }

  var immichApp;

  function buildImmichPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-immich";
    page.className = "sp-page";

    var wrap = document.createElement("div");
    wrap.className = "sp-settings-wrap";
    page.appendChild(wrap);

    parent.appendChild(page);
    els.immichPage = page;
    immichApp = wrap;
  }

  function buildSettingsPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-settings";
    page.className = "sp-page";

    var wrap = document.createElement("div");
    wrap.className = "sp-settings-wrap";
    page.appendChild(wrap);

    parent.appendChild(page);
    els.settingsPage = page;
    app = wrap;
  }

  function buildLogsPage(parent) {
    var page = document.createElement("div");
    page.id = "sp-logs";
    page.className = "sp-page";

    var toolbar = document.createElement("div");
    toolbar.className = "sp-log-toolbar";
    var clearBtn = document.createElement("button");
    clearBtn.className = "sp-log-clear";
    clearBtn.textContent = "Clear";
    clearBtn.addEventListener("click", function () { els.logOutput.innerHTML = ""; });
    toolbar.appendChild(clearBtn);
    page.appendChild(toolbar);

    var output = document.createElement("div");
    output.className = "sp-log-output";
    page.appendChild(output);
    els.logOutput = output;

    parent.appendChild(page);
    els.logsPage = page;
  }

  function switchTab(tab) {
    webUiTabs().forEach(function (tabSpec) {
      var t = tabSpec.id;
      els["tab_" + t].className = "sp-tab" + (tab === t ? " active" : "");
      els["tab_" + t].setAttribute("aria-selected", tab === t ? "true" : "false");
    });
    els.immichPage.className = "sp-page" + (tab === "immich" ? " active" : "");
    els.settingsPage.className = "sp-page" + (tab === "settings" ? " active" : "");
    els.logsPage.className = "sp-page" + (tab === "logs" ? " active" : "");
  }

    function eid(domain, name) {
    return "/" + domain + "/" + encodeURIComponent(name);
  }

  function entityStringParts(entity) {
    entity = typeof entity === "string" ? entity : "";
    var slash = entity.indexOf("/");
    if (slash > 0) {
      return {
        domain: entity.slice(0, slash),
        name: entity.slice(slash + 1)
      };
    }
    return null;
  }

  function productSettingEntityParts(key) {
    var spec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    return entityStringParts(spec && spec.entity);
  }

  function settingEntityParts(key) {
    var parts = productSettingEntityParts(key);
    if (!parts && STATIC_ENTITIES && STATIC_ENTITIES[key]) {
      parts = entityStringParts(STATIC_ENTITIES[key].entity);
    }
    if (!parts && MANUAL_ENTITIES && MANUAL_ENTITIES[key]) {
      parts = entityStringParts(MANUAL_ENTITIES[key].entity);
    }
    return parts;
  }

  function settingEntityDomain(key) {
    var parts = settingEntityParts(key);
    return parts && parts.domain ? parts.domain : "";
  }

  var endpoints = {};

  function registerManualEntityEndpoints() {
    if (!MANUAL_ENTITIES) return;
    Object.keys(MANUAL_ENTITIES).forEach(function (key) {
      var parts = entityStringParts(MANUAL_ENTITIES[key] && MANUAL_ENTITIES[key].entity);
      if (!parts) return;
      endpoints[key] = eid(parts.domain, parts.name);
    });
  }

  function registerStaticEntityEndpoints() {
    if (!STATIC_ENTITIES) return;
    Object.keys(STATIC_ENTITIES).forEach(function (key) {
      var parts = entityStringParts(STATIC_ENTITIES[key] && STATIC_ENTITIES[key].entity);
      if (!parts) return;
      endpoints[key] = eid(parts.domain, parts.name);
    });
  }

  function registerProductSettingEndpoints() {
    if (!PRODUCT_SETTINGS) return;
    Object.keys(PRODUCT_SETTINGS).forEach(function (key) {
      var parts = productSettingEntityParts(key);
      if (!parts) return;
      endpoints[key] = eid(parts.domain, parts.name);
    });
  }

  registerManualEntityEndpoints();
  registerStaticEntityEndpoints();
  registerProductSettingEndpoints();

  function post(url, params) {
    var fullUrl = params ? url + "?" + new URLSearchParams(params).toString() : url;
    return fetch(fullUrl, { method: "POST" }).then(function (r) {
      if (!r.ok) console.error("POST " + fullUrl + " failed: " + r.status);
      return r;
    }).catch(function (err) {
      console.error("POST " + fullUrl + " error:", err);
      showBanner("Failed to save setting", "error");
    });
  }

  // Matches the ESPHome template text max_length for album/person/tag ID and label lists.
  var MAX_PHOTO_ID_FIELD_LENGTH = 255;
  var MAX_NTP_SERVER_LENGTH = 253;
  var MAX_FIRMWARE_URL_LENGTH = 255;
  var PHOTO_ID_FIELD_TOO_LONG =
    "List exceeds 255 characters (device limit). Remove IDs or shorten the list.";
  var PHOTO_LABEL_FIELD_TOO_LONG =
    "Labels exceed 255 characters (device limit). Shorten or remove labels.";

  function postTextValueSet(url, value, useQueryFallback) {
    var body = new URLSearchParams();
    body.set("value", value == null ? "" : String(value));
    var encoded = body.toString();
    var fullUrl = url;
    if (useQueryFallback) {
      var candidate = url + "?" + encoded;
      if (candidate.length <= 120) fullUrl = candidate;
    }
    return fetch(fullUrl, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: encoded
    }).then(function (r) {
      if (!r.ok) console.error("POST " + fullUrl + " failed: " + r.status);
      return r;
    }).catch(function (err) {
      console.error("POST " + fullUrl + " error:", err);
      showBanner("Failed to save setting", "error");
    });
  }

  function delayMs(ms) {
    return new Promise(function (resolve) { setTimeout(resolve, ms); });
  }

  function saveConnectionValue(path, value, useQueryFallback) {
    return postTextValueSet(path + "/set", value, useQueryFallback).then(function (r) {
      if (!r || !r.ok) throw new Error("save_failed");
      return delayMs(1200);
    });
  }

  function connectionResponseValue(resp) {
    return (resp && (resp.value || resp.state)) || "";
  }

  function saveAndVerifyConnectionValue(path, value, useQueryFallback, isSaved) {
    return saveConnectionValue(path, value, useQueryFallback)
      .then(function () {
        return safeGet(path);
      })
      .then(function (resp) {
        var saved = connectionResponseValue(resp);
        if (isSaved && !isSaved(saved)) throw new Error("verify_failed");
        return saved;
      });
  }

  function saveAndVerifyConnection(url, key) {
    var normalizedUrl = normalizeImmichUrl(url);
    var apiKey = String(key || "").trim();
    if (!normalizedUrl || !apiKey) return Promise.reject(new Error("missing_connection"));
    return saveConnectionValue(endpoints.immich_url, normalizedUrl, true)
      .then(function () {
        return saveConnectionValue(endpoints.api_key, apiKey, false);
      })
      .then(function () {
        return Promise.all([
          safeGet(endpoints.immich_url),
          safeGet(endpoints.api_key)
        ]);
      })
      .then(function (res) {
        var savedUrl = normalizeImmichUrl(connectionResponseValue(res[0]));
        var savedKey = connectionResponseValue(res[1]);
        if (savedUrl !== normalizedUrl || !savedKey) throw new Error("verify_failed");
        S.immich_url = normalizedUrl;
        S.api_key = apiKey;
        return { url: normalizedUrl, key: apiKey };
      });
  }

  var PHOTO_SOURCE_APPLY_SETTING_KEYS = [
    "photo_source",
    "album_order",
    "album_ids",
    "person_ids",
    "tag_ids",
    "date_filter_enabled",
    "date_filter_mode",
    "date_from",
    "date_to",
    "relative_amount",
    "relative_unit"
  ];

  function settingUsesPhotoSourceApply(key) {
    return PHOTO_SOURCE_APPLY_SETTING_KEYS.indexOf(key) !== -1;
  }

  function saveGenericSetting(key, value) {
    if (!key || !endpoints[key]) return Promise.resolve(null);
    var domain = settingEntityDomain(key);
    var savedValue = value;
    if (domain === "switch") savedValue = !!value;
    if (domain === "number") {
      var numberValue = Number(value);
      if (isFinite(numberValue)) savedValue = numberValue;
    }
    if (domain === "select" || domain === "text") savedValue = value == null ? "" : String(value);
    S[key] = savedValue;
    if (domain === "switch") {
      return post(endpoints[key] + (savedValue ? "/turn_on" : "/turn_off"));
    }
    if (domain === "select") {
      return post(endpoints[key] + "/set", { option: savedValue });
    }
    if (domain === "number") {
      return post(endpoints[key] + "/set", { value: savedValue });
    }
    if (domain === "text") {
      return postTextValueSet(endpoints[key] + "/set", savedValue);
    }
    return Promise.resolve(null);
  }

  function saveNtpServer(key, value) {
    var server = normalizeNtpServer(value);
    S[key] = server;
    return postTextValueSet(endpoints[key] + "/set", server);
  }

  function saveScheduleWakeTimeoutSetting(key, value) {
    return saveGenericSetting(key, normalizeScheduleWakeTimeout(value));
  }

  function saveScreenRotationSetting(key, value) {
    var rotation = String(value);
    if (screenRotationOptionsForUi().indexOf(rotation) === -1) return Promise.resolve(null);
    S.screen_rotation = rotation;
    S.portrait_pairing = !isPortraitScreenRotation(rotation);
    return Promise.all([
      saveGenericSetting("screen_rotation", rotation),
      saveGenericSetting("portrait_pairing", S.portrait_pairing)
    ]);
  }

  var SETTING_SAVE_ADAPTERS = {
    ntp_server_1: saveNtpServer,
    ntp_server_2: saveNtpServer,
    ntp_server_3: saveNtpServer,
    schedule_wake_timeout: saveScheduleWakeTimeoutSetting,
    screen_rotation: saveScreenRotationSetting
  };

  function saveSetting(key, value, options) {
    var opts = options || {};
    var adapter = SETTING_SAVE_ADAPTERS[key];
    var result = adapter ? adapter(key, value, opts) : saveGenericSetting(key, value);
    if (!opts.applyPhotoSource || !settingUsesPhotoSourceApply(key)) return result;
    return Promise.resolve(result).then(function (saved) {
      return post(endpoints.apply_photo_source + "/press").then(function () {
        return saved;
      });
    });
  }

  function makeConnectionUrlField(value) {
    var f = field("Immich Server URL");
    var urlInput = input("url", value, "http://192.168.0.1:2283");
    f.appendChild(urlInput);
    return { field: f, input: urlInput };
  }

  function makeApiKeyInputGroup(options) {
    var opts = options || {};
    var grp = el("div", "input-group");
    var keyInput = input(opts.type || "text", opts.value || "", opts.placeholder || "Your Immich API key");
    var button = null;
    grp.appendChild(keyInput);
    if (opts.toggleVisibility) {
      button = el("button", "btn btn-secondary");
      button.textContent = "Show";
      button.type = "button";
      button.onclick = function () {
        var isPass = keyInput.type === "password";
        keyInput.type = isPass ? "text" : "password";
        button.textContent = isPass ? "Hide" : "Show";
      };
      grp.appendChild(button);
    } else if (opts.buttonText) {
      button = el("button", opts.buttonClass || "btn btn-primary");
      button.textContent = opts.buttonText;
      button.type = "button";
      if (opts.onButtonClick) {
        button.onclick = function () {
          opts.onButtonClick(keyInput, button);
        };
      }
      grp.appendChild(button);
    }
    return { group: grp, input: keyInput, button: button };
  }

  function makeMaskedApiKeyRow(onChange) {
    var row = el("div", "input-group");
    var mask = el("div");
    var cb = el("button", "btn btn-secondary");
    mask.className = "key-mask";
    mask.textContent = "\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022";
    cb.textContent = "Change";
    cb.type = "button";
    cb.onclick = onChange;
    row.appendChild(mask);
    row.appendChild(cb);
    return row;
  }

    function normalizeNtpServer(value) {
    return String(value == null ? "" : value).trim();
  }

  function stripUrlTrailingSlashes(value) {
    var url = String(value == null ? "" : value);
    while (url.length > 0 && url.charAt(url.length - 1) === "/" && !/^[a-z][a-z0-9+.-]*:\/\/$/i.test(url)) {
      url = url.slice(0, -1);
    }
    return url;
  }

  function normalizeFirmwareManifestUrl(value) {
    return stripUrlTrailingSlashes(String(value == null ? "" : value).trim());
  }

  function isValidHttpUrl(value) {
    try {
      var url = new URL(value);
      return (url.protocol === "http:" || url.protocol === "https:") && !!url.hostname;
    } catch (_) {
      return false;
    }
  }

  function extractUrlAuthority(value) {
    var url = String(value || "");
    if (url.indexOf("//") === 0) url = url.slice(2);
    return url.split(/[/?#]/)[0] || "";
  }

  function extractUrlHost(value) {
    var authority = extractUrlAuthority(value);
    var at = authority.lastIndexOf("@");
    if (at >= 0) authority = authority.slice(at + 1);
    if (!authority) return "";
    if (authority.charAt(0) === "[") {
      var close = authority.indexOf("]");
      return (close >= 0 ? authority.slice(0, close + 1) : authority).toLowerCase();
    }
    return authority.split(":")[0].toLowerCase();
  }

  function extractUrlPort(value) {
    var authority = extractUrlAuthority(value);
    var at = authority.lastIndexOf("@");
    if (at >= 0) authority = authority.slice(at + 1);
    if (!authority) return "";
    if (authority.charAt(0) === "[") {
      var close = authority.indexOf("]");
      if (close >= 0 && authority.charAt(close + 1) === ":") return authority.slice(close + 2).match(/^\d*/)[0];
      return "";
    }
    var colon = authority.indexOf(":");
    return colon >= 0 ? authority.slice(colon + 1).match(/^\d*/)[0] : "";
  }

  function urlHasExplicitPort(value) {
    return extractUrlPort(value) !== "";
  }

  function isLocalImmichHost(host) {
    if (!host) return false;
    if (host === "localhost" || host.charAt(0) === "[") return true;
    if (/^\d{1,3}(\.\d{1,3}){3}$/.test(host)) return true;
    return host.slice(-6) === ".local" || host.slice(-4) === ".lan";
  }

  function normalizeImmichUrl(value) {
    var url = stripUrlTrailingSlashes(String(value == null ? "" : value).trim());
    if (!url) return "";
    if (url.indexOf("//") === 0) {
      url = "https:" + url;
    } else if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(url)) {
      var host = extractUrlHost(url);
      var port = extractUrlPort(url);
      var useHttp = isLocalImmichHost(host) || urlHasExplicitPort(url);
      if (port === "443") useHttp = false;
      url = (useHttp ? "http://" : "https://") + url;
    }
    return stripUrlTrailingSlashes(url.replace(/^([a-z][a-z0-9+.-]*):\/\//i, function (_, scheme) {
      return scheme.toLowerCase() + "://";
    }));
  }

  function photoIdFieldLengthLimit() {
    return typeof MAX_PHOTO_ID_FIELD_LENGTH !== "undefined" ? MAX_PHOTO_ID_FIELD_LENGTH : 255;
  }

  function photoIdFieldTooLong(s) {
    return String(s != null ? s : "").trim().length > photoIdFieldLengthLimit();
  }

  function photoLabelFieldTooLong(s) {
    return String(s != null ? s : "").trim().length > photoIdFieldLengthLimit();
  }

  var UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  function isValidUuidList(str) {
    var s = str.trim();
    if (!s) return true;
    return s.split(",").every(function (id) { return UUID_RE.test(id.trim()); });
  }

  function splitPhotoIdList(str) {
    var parts = String(str || "").split(",").map(function (id) {
      return id.trim();
    }).filter(Boolean);
    return parts.length ? parts : [""];
  }

  function parsePhotoLabelList(str) {
    var raw = String(str || "").trim();
    if (!raw) return [];
    try {
      var parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) return parsed.map(function (label) { return String(label || ""); });
    } catch (_) {}
    return raw.split(",").map(function (label) { return label.trim(); });
  }

if (typeof module !== "undefined") {
  module.exports = {
    extractUrlHost: extractUrlHost,
    extractUrlPort: extractUrlPort,
    isValidHttpUrl: isValidHttpUrl,
    normalizeFirmwareManifestUrl: normalizeFirmwareManifestUrl,
    normalizeImmichUrl: normalizeImmichUrl,
    normalizeNtpServer: normalizeNtpServer,
    parsePhotoLabelList: parsePhotoLabelList,
    photoIdFieldTooLong: photoIdFieldTooLong,
    photoLabelFieldTooLong: photoLabelFieldTooLong,
    splitPhotoIdList: splitPhotoIdList,
    isValidUuidList: isValidUuidList
  };
}

  function developerPanelEnabledByUrl() {
    try {
      var params = new URLSearchParams(window.location.search || "");
      return params.get("developer") === "experimental" || params.get("dev") === "experimental";
    } catch (_) {
      return false;
    }
  }

  function isPortraitScreenRotation(value) {
    return value === "90" || value === "270";
  }

  function screenRotationOptionsForUi() {
    var options = productSettingOptions("screen_rotation", S.developer_features_enabled);
    return options.length ? options : ["0", "180"];
  }

  function effectiveScreenRotationForUi() {
    var current = String(S.screen_rotation || "0");
    return screenRotationOptionsForUi().indexOf(current) !== -1 ? current : "0";
  }

  function buildPhotoLabelList(idInputs, labelInputs) {
    var labels = [];
    for (var i = 0; i < idInputs.length; i++) {
      if (idInputs[i].value.trim()) labels.push(labelInputs[i].value.trim());
    }
    while (labels.length && !labels[labels.length - 1]) labels.pop();
    return labels.length ? JSON.stringify(labels) : "";
  }

  function safeGet(url) {
    return fetch(url)
      .then(function (r) {
        if (!r.ok) return null;
        return r.json();
      })
      .catch(function () {
        return null;
      });
  }

  function displayVersion(value, fallback) {
    var v = String(value || "").trim();
    if (!v) return fallback || "";
    if (v.toLowerCase() === "dev") return "Dev";
    return v;
  }

  // --- SSE-based init ---

    var evtSource = null;
  var rendered = false;
  var renderTimer = null;
  var renderAttemptInFlight = false;
  var initialSettingsRefreshStarted = false;
  var logListenerAttached = false;

  var ANSI_LEVEL = {
    "1;31": "sp-log-error",
    "0;31": "sp-log-error",
    "0;33": "sp-log-warn",
    "0;32": "sp-log-info",
    "0;35": "sp-log-config",
    "0;36": "sp-log-debug",
    "0;37": "sp-log-verbose"
  };
  var ANSI_RE = /\033\[[\d;]*m/g;

  function appendLog(msg, lvl) {
    if (!els.logOutput) return;
    var line = document.createElement("div");
    line.className = "sp-log-line";

    var ansiClass = "";
    var m = msg.match(/\033\[([\d;]+)m/);
    if (m) ansiClass = ANSI_LEVEL[m[1]] || "";

    if (ansiClass) {
      line.classList.add(ansiClass);
    } else if (lvl === 1) line.classList.add("sp-log-error");
    else if (lvl === 2) line.classList.add("sp-log-warn");
    else if (lvl === 3) line.classList.add("sp-log-info");
    else if (lvl === 4) line.classList.add("sp-log-config");
    else if (lvl === 5) line.classList.add("sp-log-debug");
    else if (lvl >= 6) line.classList.add("sp-log-verbose");

    line.textContent = msg.replace(ANSI_RE, "");

    var atBottom = els.logOutput.scrollHeight - els.logOutput.scrollTop - els.logOutput.clientHeight < 40;
    els.logOutput.appendChild(line);
    var overflow = els.logOutput.childNodes.length - WEB_UI_LOGS_RETAINED_LINES;
    if (overflow > 0) {
      for (var i = 0; i < overflow; i++)
        els.logOutput.removeChild(els.logOutput.firstChild);
    }
    if (atBottom) els.logOutput.scrollTop = els.logOutput.scrollHeight;
  }

  // Entity id -> state key mapping; optional optionsKey and default.
  var ENTITY_STATE_MAP = {};

  function registerManualStateEntities() {
    if (!MANUAL_ENTITIES) return;
    (Array.isArray(MANUAL_STATE_KEYS) ? MANUAL_STATE_KEYS : []).forEach(function (key) {
      var manualSpec = MANUAL_ENTITIES[key];
      if (!manualSpec || typeof manualSpec.entity !== "string") return;
      ENTITY_STATE_MAP[manualSpec.entity] = { key: key };
    });
  }

  function registerStaticEntities() {
    if (!STATIC_ENTITIES) return;
    Object.keys(STATIC_ENTITIES).forEach(function (key) {
      var staticSpec = STATIC_ENTITIES[key];
      if (!staticSpec || typeof staticSpec.entity !== "string") return;
      var stateSpec = { key: key };
      if (staticSpec.default !== undefined) stateSpec.default = staticSpec.default;
      if (staticSpec.optionsKey) stateSpec.optionsKey = staticSpec.optionsKey;
      if (staticSpec.boolFromState) stateSpec.boolFromState = true;
      if (staticSpec.number) stateSpec.number = true;
      ENTITY_STATE_MAP[staticSpec.entity] = stateSpec;
    });
  }

  function registerProductSettingEntities() {
    if (!PRODUCT_SETTINGS) return;
    Object.keys(PRODUCT_SETTINGS).forEach(function (key) {
      var productSpec = PRODUCT_SETTINGS[key];
      if (!productSpec || typeof productSpec.entity !== "string") return;
      var stateSpec = { key: key, default: productSpec.default };
      if (productSpec.domain === "switch") stateSpec.boolFromState = true;
      if (productSpec.domain === "number") stateSpec.number = true;
      ENTITY_STATE_MAP[productSpec.entity] = stateSpec;
    });
  }

  registerManualStateEntities();
  registerStaticEntities();
  registerProductSettingEntities();

  function registerEntityAliases() {
    if (!ENTITY_ALIASES) return;
    Object.keys(ENTITY_ALIASES).forEach(function (key) {
      var aliases = ENTITY_ALIASES[key];
      if (!Array.isArray(aliases)) return;
      aliases.forEach(function (aliasSpec) {
        if (!aliasSpec || typeof aliasSpec.entity !== "string") return;
        var stateSpec = { key: key };
        if (aliasSpec.default !== undefined) stateSpec.default = aliasSpec.default;
        if (aliasSpec.optionsKey) stateSpec.optionsKey = aliasSpec.optionsKey;
        if (aliasSpec.boolFromState) stateSpec.boolFromState = true;
        if (aliasSpec.number) stateSpec.number = true;
        ENTITY_STATE_MAP[aliasSpec.entity] = stateSpec;
      });
    });
  }

  registerEntityAliases();

  function applyEntityToState(d) {
    if (!d || !d.id) return;
    var id = d.id;
    if (id === "light/Screen: Backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null) {
        S.brightness = Math.round((d.brightness / 255) * 100);
        S.brightness_current = S.brightness;
      }
      return;
    }
    if (id === "update/Firmware: Update") {
      S.installed_version = d.current_version || "";
      S.latest_version = d.latest_version || "";
      S.update_available =
        S.installed_version &&
        S.latest_version &&
        S.installed_version !== S.latest_version;
      return;
    }
    var spec = ENTITY_STATE_MAP[id];
    if (!spec) return;
    var v = d.value != null ? d.value : d.state;
    if (spec.boolFromState) {
      S[spec.key] = v === true || v === "ON";
    } else if (spec.number) {
      S[spec.key] = v != null ? Math.round(Number(v)) : (spec.default !== undefined ? spec.default : 0);
    } else {
      S[spec.key] = v !== undefined && v !== null ? String(v) : (spec.default !== undefined ? spec.default : "");
    }
    if (spec.key === "timezone") S[spec.key] = normalizeTimezoneOption(S[spec.key]);
    if (spec.key && spec.key.indexOf("ntp_server_") === 0) S[spec.key] = normalizeNtpServer(S[spec.key]);
    if (spec.optionsKey && d.option && d.option.length) S[spec.optionsKey] = d.option;
    if (spec.key === "photo_metadata_date_format" &&
        S[spec.key] !== "Relative Date" && S[spec.key] !== "Date Taken") {
      S.photo_metadata_date_taken_format = normalizeDateTakenFormat(S[spec.key]);
      S[spec.key] = "Date Taken";
    }
    if (spec.key === "photo_metadata_date_taken_format") {
      S[spec.key] = normalizeDateTakenFormat(S[spec.key]);
    }
  }

  function normalizeDateTakenFormat(value) {
    if (value === "January 1, 2026" || value === "January 1, 2000" || value === "Month Day, Year" ||
        value === "Month Day Ordinal, Year") {
      return "January 1, 2026";
    }
    return "1 January, 2026";
  }

  function collectState(d) {
    applyEntityToState(d);
  }

  // Generated from product metadata plus status-only fields; KEY_TO_ENTITY_ID derived from ENTITY_STATE_MAP.
  var INITIAL_FETCH_KEYS = ["firmware","photo_source","album_order","date_filter_mode","relative_unit","photo_orientation","display_mode","interval","conn_timeout","screen_rotation","photo_metadata_date_format","photo_metadata_date_taken_format","clock_format","update_frequency","auto_update","firmware_manifest_url","date_filter_enabled","date_from","date_to","relative_amount","schedule_enabled","schedule_on_hour","schedule_off_hour","schedule_wake_timeout","brightness_day","brightness_night","base_tone_enabled","base_tone","warm_tones_enabled","warm_tone_intensity","warm_tone_override","portrait_pairing","photo_metadata_date_enabled","photo_metadata_location_enabled","timezone","ntp_server_1","ntp_server_2","ntp_server_3","album_ids","album_labels","person_ids","person_labels","tag_ids","tag_labels","sunrise","sunset","developer_features_enabled","c6_current_firmware","c6_available_firmware"];
  function getEntityIdForStateKey(key) {
    var productSpec = PRODUCT_SETTINGS && PRODUCT_SETTINGS[key];
    if (productSpec && typeof productSpec.entity === "string") return productSpec.entity;
    for (var id in ENTITY_STATE_MAP) {
      if (ENTITY_STATE_MAP[id].key === key) return id;
    }
    return null;
  }
  var KEY_TO_ENTITY_ID = {};
  INITIAL_FETCH_KEYS.forEach(function (k) {
    var id = getEntityIdForStateKey(k);
    if (id) KEY_TO_ENTITY_ID[k] = id;
  });

  function fetchDeviceSettingsState() {
    var urls = INITIAL_FETCH_KEYS.map(function (k) {
      if (!endpoints[k]) {
        console.error("Missing endpoint for startup setting:", k);
        return Promise.resolve(null);
      }
      return safeGet(endpoints[k]);
    });
    return Promise.all(urls).then(function (res) {
      for (var i = 0; i < res.length; i++) {
        var data = res[i];
        if (!data) continue;
        applyEntityToState({
          id: KEY_TO_ENTITY_ID[INITIAL_FETCH_KEYS[i]],
          value: data.value,
          state: data.state,
          option: data.option
        });
      }
    });
  }

  function isEditingSetting() {
    var active = document.activeElement;
    if (!active || !els.root || !els.root.contains(active)) return false;
    return /^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(active.tagName);
  }

  function liveRenderStateKeyHasPrefix(key) {
    return (Array.isArray(LIVE_RENDER_STATE_PREFIXES) ? LIVE_RENDER_STATE_PREFIXES : []).some(function (prefix) {
      return key.indexOf(prefix) === 0;
    });
  }

  function renderConfiguredSettingsPage() {
    renderSettings();

    if (initialSettingsRefreshStarted) return;
    initialSettingsRefreshStarted = true;

    // Draw the cards first. The ESP webserver can take a while to answer every
    // per-entity request, so hydrate the values in the background.
    fetchDeviceSettingsState().then(function () {
      if (rendered && !isEditingSetting()) renderSettings();
    });
  }

  function scheduleTryRender(delayMs) {
    if (rendered || renderAttemptInFlight || renderTimer) return;
    renderTimer = setTimeout(function () {
      renderTimer = null;
      tryRender();
    }, delayMs);
  }

  function showConfiguredSettings() {
    rendered = true;
    renderAttemptInFlight = false;
    renderConfiguredSettingsPage();
  }

  function tryRender() {
    if (rendered || renderAttemptInFlight) return;
    if (renderTimer) {
      clearTimeout(renderTimer);
      renderTimer = null;
    }
    if (S.immich_url) {
      showConfiguredSettings();
      return;
    }
    renderAttemptInFlight = true;
    Promise.all([
      safeGet(endpoints.immich_url),
      safeGet(endpoints.api_key)
    ]).then(function (res) {
      renderAttemptInFlight = false;
      if (rendered) return;
      if (res[0]) S.immich_url = normalizeImmichUrl(res[0].value || res[0].state || "");
      if (res[1]) S.api_key = res[1].value || res[1].state || "";
      if (S.immich_url) {
        showConfiguredSettings();
      } else {
        rendered = true;
        renderWizard();
      }
    });
  }

  function initSSE() {
    try {
      evtSource = new EventSource("/events");

      evtSource.addEventListener("state", function (e) {
        try {
          var d = JSON.parse(e.data);
          collectState(d);
          if (rendered) handleLiveEvent(d);
        } catch (_) {}

        if (!rendered) {
          if (S.immich_url) showConfiguredSettings();
          else scheduleTryRender(250);
        }
      });

      if (!logListenerAttached) {
        logListenerAttached = true;
        evtSource.addEventListener("log", function (e) {
          var d;
          try { d = JSON.parse(e.data); } catch (_) { d = { msg: e.data }; }
          appendLog(d.msg || e.data, d.lvl);
        });
      }

      evtSource.onerror = function () {
        if (!rendered) {
          scheduleTryRender(1000);
        }
      };

      evtSource.onopen = function () {};
    } catch (_) {
      tryRender();
    }

    scheduleTryRender(250);
    setTimeout(function () {
      if (!rendered) tryRender();
    }, 5000);
  }

  // --- Wizard ---

    function renderWizard() {
    var step = 1;
    immichApp.innerHTML = "";
    app.innerHTML = "";
    renderStartupDevicePage();
    var wrap = el("div", "fade-in");
    wrap.innerHTML =
      '<p class="subtitle">Let\'s connect your photo frame</p>';
    var steps = el("div", "wizard-steps");
    var s1 = el("div", "step active");
    var s2 = el("div", "step");
    steps.appendChild(s1);
    steps.appendChild(s2);
    wrap.appendChild(steps);

    var body = el("div");
    wrap.appendChild(body);
    immichApp.appendChild(wrap);

    function showStep() {
      body.innerHTML = "";
      if (step === 1) {
        s1.className = "step active";
        s2.className = "step";
        body.appendChild(renderStep1());
      } else {
        s1.className = "step done";
        s2.className = "step active";
        body.appendChild(renderStep2());
      }
    }

    function renderStep1() {
      var card = el("div", "card fade-in");
      card.innerHTML = "<h3>Connection</h3>";

      var urlField = makeConnectionUrlField(S.immich_url);
      var urlInput = urlField.input;
      card.appendChild(urlField.field);

      var f2 = field("API Key");
      var keyControl = makeApiKeyInputGroup({
        type: "password",
        value: S.api_key,
        placeholder: "Your Immich API key",
        toggleVisibility: true
      });
      var keyInput = keyControl.input;
      f2.appendChild(keyControl.group);
      card.appendChild(f2);

      var nav = el("div", "wizard-nav");
      var nextBtn = el("button", "btn btn-primary");
      nextBtn.textContent = "Connect";
      nextBtn.onclick = function () {
        var u = normalizeImmichUrl(urlInput.value);
        var k = keyInput.value.trim();
        if (!u || !k) return;
        nextBtn.disabled = true;
        nextBtn.textContent = "Saving\u2026";
        saveAndVerifyConnection(u, k)
          .then(function (connection) {
            urlInput.value = connection.url;
            step = 2;
            showStep();
          })
          .catch(function () {
            nextBtn.disabled = false;
            nextBtn.textContent = "Connect";
            showBanner("Failed to save connection. Please try again.", "error");
          });
      };
      nav.appendChild(nextBtn);
      card.appendChild(nav);
      return card;
    }

    function renderStep2() {
      var card = el("div", "card fade-in");
      card.innerHTML = "<h3>Clock & timezone</h3>";

      var f1 = field("Clock Format");
      f1.appendChild(
        selectFromOptions(productSettingOptions("clock_format"), S.clock_format, function (v) {
          saveSetting("clock_format", v);
        })
      );
      card.appendChild(f1);

      var f2 = field("Timezone");
      f2.appendChild(
        timezoneSelect(S.tz_options, S.timezone, function (v) {
          saveSetting("timezone", v);
        })
      );
      card.appendChild(f2);

      card.appendChild(ntpServersField());

      var nav = el("div", "wizard-nav");
      var backBtn = el("button", "btn btn-secondary");
      backBtn.textContent = "Back";
      backBtn.onclick = function () {
        step = 1;
        showStep();
      };
      var doneBtn = el("button", "btn btn-primary");
      doneBtn.textContent = "Done";
      doneBtn.onclick = function () {
        renderSettings();
      };
      nav.appendChild(backBtn);
      nav.appendChild(doneBtn);
      card.appendChild(nav);
      return card;
    }

    showStep();
  }

  function renderStartupDevicePage() {
    var wrap = el("div", "fade-in");
    wrap.appendChild(makeImportSettingsCard());
    app.appendChild(wrap);
  }

  // --- Settings ---

    function makeConnectionCard() {
    // Connection
    var connBody = el("div");
    var connStatus = el("div", "status mb-12");
    connStatus.id = "conn-status";

    function showSaved(msg) {
      setStatus(connStatus, msg || "Saved", "green", 3000);
    }

    function showConnectionError(msg) {
      setStatus(connStatus, msg, "red");
    }

    var urlField = makeConnectionUrlField(S.immich_url);
    var urlInput = urlField.input;
    urlInput.onchange = function () {
      var normalized = normalizeImmichUrl(urlInput.value);
      saveAndVerifyConnectionValue(
        endpoints.immich_url,
        normalized,
        true,
        function (saved) { return normalizeImmichUrl(saved) === normalized; }
      ).then(function () {
        S.immich_url = normalized;
        urlInput.value = normalized;
        showSaved("URL saved");
      }).catch(function () {
        showConnectionError("Failed to save URL");
      });
    };
    connBody.appendChild(urlField.field);

    var f2 = field("API Key");
    var keyConfigured = S.api_key && S.api_key.length > 0;
    var keyWrap = el("div");

    function showKeyMasked() {
      keyWrap.innerHTML = "";
      keyWrap.appendChild(makeMaskedApiKeyRow(function () {
        keyWrap.innerHTML = "";
        keyWrap.appendChild(makeKeyInput());
      }));
    }

    function makeKeyInput() {
      var keyControl = makeApiKeyInputGroup({
        type: "text",
        value: "",
        placeholder: "Paste your Immich API key",
        buttonText: "Save",
        buttonClass: "btn btn-primary",
        onButtonClick: function (keyInput, saveBtn) {
          var v = keyInput.value.trim();
          if (!v) return;
          saveBtn.disabled = true;
          saveBtn.textContent = "Saving\u2026";
          saveAndVerifyConnectionValue(
            endpoints.api_key,
            v,
            false,
            function (saved) { return !!saved; }
          ).then(function () {
            S.api_key = v;
            showSaved("API key saved");
            showKeyMasked();
          }).catch(function () {
            saveBtn.disabled = false;
            saveBtn.textContent = "Save";
            showConnectionError("Failed to save API key");
          });
        }
      });
      return keyControl.group;
    }

    if (keyConfigured) {
      showKeyMasked();
    } else {
      keyWrap.appendChild(makeKeyInput());
    }

    f2.appendChild(keyWrap);
    connBody.appendChild(f2);

    connBody.appendChild(productSelectSettingField("Connection Timeout", "conn_timeout"));

    connBody.appendChild(connStatus);
    return makeCollapsibleCard("Connection", connBody, true);

  }

  function makeFrequencyCard() {
    // Frequency
    var dispBody = el("div");
    dispBody.appendChild(productSelectSettingField("Slideshow Interval", "interval"));
    return makeCollapsibleCard("Frequency", dispBody, true);

  }

  function makePhotoSourceCard() {
    // Photo Source
    var srcBody = el("div");
    var photoSourceApplyTimer = null;
    var pendingPhotoSourceSave = {
      source: false,
      album: false,
      albumLabel: false,
      albumOrder: false,
      person: false,
      personLabel: false,
      tag: false,
      tagLabel: false
    };
    var fSrc = field("Source");
    var srcSel = selectFromOptions(productSettingOptions("photo_source"), S.photo_source, function (v) {
      S.photo_source = v;
      albumField.style.display = v === "Album" ? "" : "none";
      albumOrderField.style.display = v === "Album" ? "" : "none";
      personField.style.display = v === "Person" ? "" : "none";
      tagField.style.display = v === "Tag" ? "" : "none";
      schedulePhotoSourceApply(0, { source: true });
    });

    var albumOrderField = field("Album Order");
    albumOrderField.appendChild(
      selectFromOptions(productSettingOptions("album_order"), S.album_order, function (v) {
        S.album_order = v;
        schedulePhotoSourceApply(0, { albumOrder: true });
      })
    );
    albumOrderField.style.display = S.photo_source === "Album" ? "" : "none";

    var albumList = photoIdListField({
      label: "Albums",
      idKey: "album_ids",
      labelKey: "album_labels",
      idPlaceholder: "Paste album ID from Immich URL",
      labelPlaceholder: "What is it?",
      addText: "Add an album",
      removeTitle: "Remove album ID",
      reorderable: true,
      moveUpTitle: "Move album up",
      moveDownTitle: "Move album down",
      idChanges: { album: true, albumLabel: true },
      labelChanges: { albumLabel: true },
      clearChanges: { album: true, albumLabel: true },
      onChange: function (changes, delayMs) { schedulePhotoSourceApply(delayMs, changes); }
    });
    var albumField = albumList.field;
    albumField.style.display = S.photo_source === "Album" ? "" : "none";

    var personList = photoIdListField({
      label: "People",
      idKey: "person_ids",
      labelKey: "person_labels",
      idPlaceholder: "Paste person ID from Immich URL",
      labelPlaceholder: "Who is it?",
      addText: "Add a person",
      removeTitle: "Remove person ID",
      idChanges: { person: true, personLabel: true },
      labelChanges: { personLabel: true },
      clearChanges: { person: true, personLabel: true },
      onChange: function (changes, delayMs) { schedulePhotoSourceApply(delayMs, changes); }
    });
    var personField = personList.field;
    personField.style.display = S.photo_source === "Person" ? "" : "none";

    var tagList = photoIdListField({
      label: "Tags",
      idKey: "tag_ids",
      labelKey: "tag_labels",
      idPlaceholder: "Paste tag ID from Immich URL",
      labelPlaceholder: "What tag is it?",
      addText: "Add a tag",
      removeTitle: "Remove tag ID",
      idChanges: { tag: true, tagLabel: true },
      labelChanges: { tagLabel: true },
      clearChanges: { tag: true, tagLabel: true },
      onChange: function (changes, delayMs) { schedulePhotoSourceApply(delayMs, changes); }
    });
    var tagField = tagList.field;
    tagField.style.display = S.photo_source === "Tag" ? "" : "none";

    function validatePhotoSourceInputs(changes) {
      albumList.error.textContent = "";
      personList.error.textContent = "";
      tagList.error.textContent = "";
      var srcVal = srcSel.value;
      var albumTrim = albumList.getIdsValue();
      var albumLabels = albumList.getLabelsValue();
      var personTrim = personList.getIdsValue();
      var personLabels = personList.getLabelsValue();
      var tagTrim = tagList.getIdsValue();
      var tagLabels = tagList.getLabelsValue();
      var shouldValidateAlbum = changes.album || srcVal === "Album";
      var shouldValidatePerson = changes.person || srcVal === "Person";
      var shouldValidateTag = changes.tag || srcVal === "Tag";
      if (shouldValidateAlbum && photoIdFieldTooLong(albumTrim)) {
        albumList.error.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidatePerson && photoIdFieldTooLong(personTrim)) {
        personList.error.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateTag && photoIdFieldTooLong(tagTrim)) {
        tagList.error.textContent = PHOTO_ID_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateAlbum && !isValidUuidList(albumTrim)) {
        albumList.error.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.albumLabel && photoLabelFieldTooLong(albumLabels)) {
        albumList.error.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidatePerson && !isValidUuidList(personTrim)) {
        personList.error.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.personLabel && photoLabelFieldTooLong(personLabels)) {
        personList.error.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      if (shouldValidateTag && !isValidUuidList(tagTrim)) {
        tagList.error.textContent = "Invalid UUID format";
        return null;
      }
      if (changes.tagLabel && photoLabelFieldTooLong(tagLabels)) {
        tagList.error.textContent = PHOTO_LABEL_FIELD_TOO_LONG;
        return null;
      }
      return {
        source: srcVal,
        albumOrder: S.album_order,
        albumIds: albumTrim,
        albumLabels: albumLabels,
        personIds: personTrim,
        personLabels: personLabels,
        tagIds: tagTrim,
        tagLabels: tagLabels
      };
    }
    function applyPhotoSourceInputs() {
      var changes = {
        source: pendingPhotoSourceSave.source,
        album: pendingPhotoSourceSave.album,
        albumLabel: pendingPhotoSourceSave.albumLabel,
        albumOrder: pendingPhotoSourceSave.albumOrder,
        person: pendingPhotoSourceSave.person,
        personLabel: pendingPhotoSourceSave.personLabel,
        tag: pendingPhotoSourceSave.tag,
        tagLabel: pendingPhotoSourceSave.tagLabel
      };
      pendingPhotoSourceSave = {
        source: false,
        album: false,
        albumLabel: false,
        albumOrder: false,
        person: false,
        personLabel: false,
        tag: false,
        tagLabel: false
      };
      var vals = validatePhotoSourceInputs(changes);
      if (!vals) return;
      var requests = [];
      if (changes.source) {
        requests.push(saveSetting("photo_source", vals.source));
      }
      if (changes.album) {
        requests.push(saveSetting("album_ids", vals.albumIds));
      }
      if (changes.albumLabel) {
        requests.push(saveSetting("album_labels", vals.albumLabels));
      }
      if (changes.albumOrder) {
        requests.push(saveSetting("album_order", vals.albumOrder));
      }
      if (changes.person) {
        requests.push(saveSetting("person_ids", vals.personIds));
      }
      if (changes.personLabel) {
        requests.push(saveSetting("person_labels", vals.personLabels));
      }
      if (changes.tag) {
        requests.push(saveSetting("tag_ids", vals.tagIds));
      }
      if (changes.tagLabel) {
        requests.push(saveSetting("tag_labels", vals.tagLabels));
      }
      if (!requests.length) return;
      Promise.all(requests).then(function () {
        if (changes.source || changes.album || changes.albumOrder || changes.person || changes.tag)
          post(endpoints.apply_photo_source + "/press");
      });
    }
    function schedulePhotoSourceApply(delayMs, changes) {
      if (changes) {
        pendingPhotoSourceSave.source = pendingPhotoSourceSave.source || !!changes.source;
        pendingPhotoSourceSave.album = pendingPhotoSourceSave.album || !!changes.album;
        pendingPhotoSourceSave.albumLabel = pendingPhotoSourceSave.albumLabel || !!changes.albumLabel;
        pendingPhotoSourceSave.albumOrder = pendingPhotoSourceSave.albumOrder || !!changes.albumOrder;
        pendingPhotoSourceSave.person = pendingPhotoSourceSave.person || !!changes.person;
        pendingPhotoSourceSave.personLabel = pendingPhotoSourceSave.personLabel || !!changes.personLabel;
        pendingPhotoSourceSave.tag = pendingPhotoSourceSave.tag || !!changes.tag;
        pendingPhotoSourceSave.tagLabel = pendingPhotoSourceSave.tagLabel || !!changes.tagLabel;
      }
      clearTimeout(photoSourceApplyTimer);
      photoSourceApplyTimer = setTimeout(applyPhotoSourceInputs, delayMs == null ? 600 : delayMs);
    }

    fSrc.appendChild(srcSel);
    srcBody.appendChild(fSrc);
    srcBody.appendChild(albumOrderField);
    srcBody.appendChild(albumField);
    srcBody.appendChild(personField);
    srcBody.appendChild(tagField);

    return makeCollapsibleCard("Photo Source", srcBody, true);

  }

  function makeAdvancedFiltersCard() {
    // Advanced Filters
    var DATE_RE = /^\d{4}-\d{2}-\d{2}$/;
    function isValidDate(s) {
      if (!DATE_RE.test(s)) return false;
      var parts = s.split("-");
      var d = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
      return d.getFullYear() === Number(parts[0]) && d.getMonth() === Number(parts[1]) - 1 && d.getDate() === Number(parts[2]);
    }
    function isFilterActive(enabled) {
      return !!enabled;
    }
    var filterBadge = makeBadge(isFilterActive(S.date_filter_enabled));
    var filterBody = el("div");
    var filterApplyTimer = null;
    var filterDetails = el("div");
    filterDetails.style.display = S.date_filter_enabled ? "" : "none";
    filterBody.appendChild(toggleSettingRow({
      label: "Filter by Date",
      value: S.date_filter_enabled,
      getValue: function () { return S.date_filter_enabled; },
      setValue: function (value) { S.date_filter_enabled = value; },
      details: filterDetails,
      badge: filterBadge,
      badgeActive: function () { return isFilterActive(S.date_filter_enabled); },
      onChange: scheduleFilterApply
    }).field);

    var fFilterMode = field("Mode");
    var modeVal = S.date_filter_mode;
    var modeSegment = segmentedControl(productSettingOptions("date_filter_mode"), modeVal, function (v) {
      modeVal = v;
      updateFilterModeDisplay(v);
      scheduleFilterApply();
    }, function (v) {
      return v === "Relative Range" ? "Relative" : "Fixed";
    });
    fFilterMode.appendChild(modeSegment);
    filterDetails.appendChild(fFilterMode);

    var fixedWrap = el("div");
    var fDateFrom = field("From");
    var dateFromInput = document.createElement("input");
    dateFromInput.type = "date";
    dateFromInput.value = S.date_from || "";
    dateFromInput.placeholder = "YYYY-MM-DD";
    var dateFromError = el("div", "field-error");
    fDateFrom.appendChild(dateFromInput);
    fDateFrom.appendChild(dateFromError);
    fixedWrap.appendChild(fDateFrom);

    var fDateTo = field("Until");
    var dateToInput = document.createElement("input");
    dateToInput.type = "date";
    dateToInput.value = S.date_to || "";
    dateToInput.placeholder = "YYYY-MM-DD";
    var dateToError = el("div", "field-error");
    fDateTo.appendChild(dateToInput);
    fDateTo.appendChild(dateToError);
    fixedWrap.appendChild(fDateTo);
    filterDetails.appendChild(fixedWrap);

    var relativeWrap = el("div", "filter-relative-row");
    var fRelativeAmount = field("Last");
    var relativeAmountInput = document.createElement("input");
    var relativeAmountMin = productNumberMin("relative_amount", 1);
    var relativeAmountMax = productNumberMax("relative_amount", 120);
    var relativeAmountStep = productNumberStep("relative_amount", 1);
    relativeAmountInput.type = "number";
    relativeAmountInput.min = String(relativeAmountMin);
    relativeAmountInput.max = String(relativeAmountMax);
    relativeAmountInput.step = String(relativeAmountStep);
    relativeAmountInput.value = String(S.relative_amount || 1);
    var relativeAmountError = el("div", "field-error");
    fRelativeAmount.appendChild(relativeAmountInput);
    fRelativeAmount.appendChild(relativeAmountError);
    relativeWrap.appendChild(fRelativeAmount);

    var fRelativeUnit = field("Unit");
    var relativeUnitSelect = selectFromOptions(productSettingOptions("relative_unit"), S.relative_unit, function () {
      scheduleFilterApply();
    });
    fRelativeUnit.appendChild(relativeUnitSelect);
    relativeWrap.appendChild(fRelativeUnit);
    filterDetails.appendChild(relativeWrap);

    function updateFilterModeDisplay(mode) {
      fixedWrap.style.display = mode === "Relative Range" ? "none" : "";
      relativeWrap.style.display = mode === "Relative Range" ? "" : "none";
    }
    updateFilterModeDisplay(S.date_filter_mode);

    var filterError = el("div", "field-error");
    filterDetails.appendChild(filterError);

    dateFromInput.onchange = scheduleFilterApply;
    dateToInput.onchange = scheduleFilterApply;
    relativeAmountInput.onchange = scheduleFilterApply;

    function readFilterValues() {
      dateFromError.textContent = "";
      dateToError.textContent = "";
      relativeAmountError.textContent = "";
      filterError.textContent = "";
      var fromVal = dateFromInput.value.trim();
      var toVal = dateToInput.value.trim();
      var amountVal = Math.round(Number(relativeAmountInput.value));
      var unitVal = relativeUnitSelect.value;
      if (S.date_filter_enabled && modeVal === "Fixed Range" && fromVal && !isValidDate(fromVal)) {
        dateFromError.textContent = "Invalid date — use YYYY-MM-DD";
        return null;
      }
      if (S.date_filter_enabled && modeVal === "Fixed Range" && toVal && !isValidDate(toVal)) {
        dateToError.textContent = "Invalid date — use YYYY-MM-DD";
        return null;
      }
      if (S.date_filter_enabled && modeVal === "Fixed Range" && fromVal && toVal && fromVal > toVal) {
        filterError.textContent = "From must not be after Until";
        return null;
      }
      if (S.date_filter_enabled && modeVal === "Relative Range" &&
          (!amountVal || amountVal < relativeAmountMin || amountVal > relativeAmountMax)) {
        relativeAmountError.textContent = "Enter a whole number from " + relativeAmountMin + " to " + relativeAmountMax;
        return null;
      }
      return { from: fromVal, to: toVal, amount: amountVal || relativeAmountMin, unit: unitVal };
    }

    function applyFilterSettings() {
      var vals = readFilterValues();
      if (!vals) return;
      S.date_filter_mode = modeVal;
      S.date_from = vals.from;
      S.date_to = vals.to;
      S.relative_amount = vals.amount;
      S.relative_unit = vals.unit;
      filterBadge.className = "on-badge" + (isFilterActive(S.date_filter_enabled) ? " active" : "");
      Promise.all([
        saveSetting("date_filter_enabled", S.date_filter_enabled),
        saveSetting("date_filter_mode", modeVal),
        saveSetting("date_from", vals.from),
        saveSetting("date_to", vals.to),
        saveSetting("relative_amount", vals.amount),
        saveSetting("relative_unit", vals.unit)
      ]).then(function () {
        post(endpoints.apply_photo_source + "/press");
      });
    }

    function scheduleFilterApply() {
      clearTimeout(filterApplyTimer);
      filterApplyTimer = setTimeout(applyFilterSettings, 300);
    }

    filterBody.appendChild(filterDetails);
    return makeCollapsibleCard("Advanced Filters", filterBody, true, filterBadge);
  }

  function makeLayoutCard() {
    // Layout
    var photoBody = el("div");

    var portraitRotationActive = isPortraitScreenRotation(effectiveScreenRotationForUi());
    var pairingEnabled = S.portrait_pairing && !portraitRotationActive;
    photoBody.appendChild(toggleSettingRow({
      label: "Portrait Pairing",
      value: pairingEnabled,
      getValue: function () { return S.portrait_pairing; },
      setValue: function (value) { S.portrait_pairing = value; },
      disabled: portraitRotationActive,
      disabledTitle: "Portrait pairing is disabled while the screen is in portrait rotation",
      onChange: function () {
        saveSetting("portrait_pairing", S.portrait_pairing);
      }
    }).field);

    var fPhotoOrientation = field("Photo Orientation");
    fPhotoOrientation.appendChild(
      selectFromOptions(productSettingOptions("photo_orientation"), S.photo_orientation, function (v) {
        saveSetting("photo_orientation", v);
      })
    );
    photoBody.appendChild(fPhotoOrientation);

    var fDisplayMode = field("Display Mode");
    fDisplayMode.appendChild(
      selectFromOptions(productSettingOptions("display_mode"), S.display_mode, function (v) {
        saveSetting("display_mode", v);
      })
    );
    photoBody.appendChild(fDisplayMode);

    return makeCollapsibleCard("Layout", photoBody, true);
  }

  function makeMetadataCard() {
    // Metadata
    function metadataIsActive() {
      return S.photo_metadata_date_enabled || S.photo_metadata_location_enabled;
    }
    var metadataBadge = makeBadge(metadataIsActive());
    var metadataBody = el("div");
    var metadataDateDetails = el("div");
    var fMetadataDateTakenFormat = null;

    function refreshMetadataDetails() {
      metadataDateDetails.style.display = S.photo_metadata_date_enabled ? "" : "none";
      if (fMetadataDateTakenFormat) {
        fMetadataDateTakenFormat.style.display =
          S.photo_metadata_date_enabled && S.photo_metadata_date_format === "Date Taken" ? "" : "none";
      }
      metadataBadge.className = "on-badge" + (metadataIsActive() ? " active" : "");
    }

    var fMetadataDate = toggleSettingRow({
      label: "Date",
      value: S.photo_metadata_date_enabled,
      getValue: function () { return S.photo_metadata_date_enabled; },
      setValue: function (value) { S.photo_metadata_date_enabled = value; },
      onChange: function () {
        refreshMetadataDetails();
        saveSetting("photo_metadata_date_enabled", S.photo_metadata_date_enabled);
      }
    }).field;

    var fMetadataDateFormat = field("Date Format");
    fMetadataDateFormat.appendChild(
      selectFromOptions(productSettingOptions("photo_metadata_date_format"), S.photo_metadata_date_format, function (v) {
        saveSetting("photo_metadata_date_format", v);
        refreshMetadataDetails();
      })
    );
    metadataDateDetails.appendChild(fMetadataDateFormat);

    fMetadataDateTakenFormat = field("Date Taken Format");
    fMetadataDateTakenFormat.appendChild(
      selectFromOptions(productSettingOptions("photo_metadata_date_taken_format"), S.photo_metadata_date_taken_format, function (v) {
        saveSetting("photo_metadata_date_taken_format", v);
      })
    );
    metadataDateDetails.appendChild(fMetadataDateTakenFormat);

    var fMetadataLocation = toggleSettingRow({
      label: "Location",
      value: S.photo_metadata_location_enabled,
      getValue: function () { return S.photo_metadata_location_enabled; },
      setValue: function (value) { S.photo_metadata_location_enabled = value; },
      onChange: function () {
        refreshMetadataDetails();
        saveSetting("photo_metadata_location_enabled", S.photo_metadata_location_enabled);
      }
    }).field;
    metadataBody.appendChild(fMetadataLocation);
    metadataBody.appendChild(fMetadataDate);
    metadataBody.appendChild(metadataDateDetails);

    refreshMetadataDetails();
    return makeCollapsibleCard("Metadata", metadataBody, true, metadataBadge);

  }

    function makeScreenBrightnessCard() {
    // Screen Brightness
    var dnDetails = el("div");

    dnDetails.appendChild(rangeSettingField("Daytime Brightness", "brightness_day", {
      minFallback: 10,
      maxFallback: 100,
      stepFallback: 5,
      valueSuffix: "%"
    }).field);

    dnDetails.appendChild(rangeSettingField("Nighttime Brightness", "brightness_night", {
      minFallback: 10,
      maxFallback: 100,
      stepFallback: 5,
      valueSuffix: "%"
    }).field);

    var fSunInfo = el("div", "field sun-info");
    fSunInfo.id = "sun-info";
    function updateSunInfo() {
      updateSunInfoElement(fSunInfo);
    }
    updateSunInfo();
    dnDetails.appendChild(fSunInfo);

    return makeCollapsibleCard("Screen Brightness", dnDetails, true);

  }

  function makeScreenToneCard() {
    // Screen Tone
    var toneBadge = makeBadge(S.base_tone_enabled || S.warm_tones_enabled);
    var warmBody = el("div");

    var baseDetails = el("div");
    baseDetails.style.display = S.base_tone_enabled ? "" : "none";
    var fBaseToneToggle = toggleSettingRow({
      label: "Screen Tone Adjustment",
      value: S.base_tone_enabled,
      getValue: function () { return S.base_tone_enabled; },
      setValue: function (value) { S.base_tone_enabled = value; },
      details: baseDetails,
      badge: toneBadge,
      badgeActive: function () { return S.base_tone_enabled || S.warm_tones_enabled; },
      onChange: function () { saveSetting("base_tone_enabled", S.base_tone_enabled); }
    }).field;
    fBaseToneToggle.style.marginBottom = "8px";
    warmBody.appendChild(fBaseToneToggle);

    baseDetails.appendChild(rangeSettingField("", "base_tone", {
      minFallback: 0,
      maxFallback: 100,
      stepFallback: 5,
      leftLabel: "Cooler",
      rightLabel: "Warmer"
    }).field);
    baseDetails.style.marginBottom = "28px";
    warmBody.appendChild(baseDetails);

    var nightDetails = el("div");
    nightDetails.style.display = S.warm_tones_enabled ? "" : "none";
    var fWarmToggle = toggleSettingRow({
      label: "Night Tone Adjustment",
      value: S.warm_tones_enabled,
      getValue: function () { return S.warm_tones_enabled; },
      setValue: function (value) { S.warm_tones_enabled = value; },
      details: nightDetails,
      badge: toneBadge,
      badgeActive: function () { return S.base_tone_enabled || S.warm_tones_enabled; },
      onChange: function () { saveSetting("warm_tones_enabled", S.warm_tones_enabled); }
    }).field;
    fWarmToggle.style.marginBottom = "8px";
    warmBody.appendChild(fWarmToggle);

    nightDetails.appendChild(rangeSettingField("", "warm_tone_intensity", {
      minFallback: 10,
      maxFallback: 100,
      stepFallback: 5,
      leftLabel: "Cooler",
      rightLabel: "Warmer"
    }).field);

    nightDetails.appendChild(toggleSettingRow({
      label: "Turn on until sunrise",
      value: S.warm_tone_override,
      getValue: function () { return S.warm_tone_override; },
      setValue: function (value) { S.warm_tone_override = value; },
      onChange: function () { saveSetting("warm_tone_override", S.warm_tone_override); }
    }).field);

    warmBody.appendChild(nightDetails);
    return makeCollapsibleCard("Screen Tone", warmBody, true, toneBadge);

  }

  function makeNightScheduleCard() {
    // Schedule
    var schedBadge = makeBadge(S.schedule_enabled);
    var schedBody = el("div");
    var schedDetails = el("div");
    schedDetails.style.display = S.schedule_enabled ? "" : "none";
    schedBody.appendChild(toggleSettingRow({
      label: "Schedule Screen Off",
      value: S.schedule_enabled,
      getValue: function () { return S.schedule_enabled; },
      setValue: function (value) { S.schedule_enabled = value; },
      details: schedDetails,
      badge: schedBadge,
      onChange: function () { saveSetting("schedule_enabled", S.schedule_enabled); }
    }).field);

    schedDetails.appendChild(hourSelectSettingField("On Time", "schedule_on_hour"));
    schedDetails.appendChild(hourSelectSettingField("Off Time", "schedule_off_hour"));

    var fWakeTimeout = field("When Woken, Idle Time To Screen Off");
    var scheduleWakeMin = productNumberMin("schedule_wake_timeout", 10);
    var scheduleWakeMax = productNumberMax("schedule_wake_timeout", 3600);
    var scheduleWakeOptions = [10, 30, 60, 120, 300, 600, 1800, 3600].filter(function (v) {
      return v >= scheduleWakeMin && v <= scheduleWakeMax;
    });
    var scheduleWakeCurrent = normalizeScheduleWakeTimeout(S.schedule_wake_timeout);
    if (scheduleWakeOptions.indexOf(scheduleWakeCurrent) === -1) {
      scheduleWakeOptions.push(scheduleWakeCurrent);
      scheduleWakeOptions.sort(function (a, b) { return a - b; });
    }
    fWakeTimeout.appendChild(
      selectFromOptions(scheduleWakeOptions, scheduleWakeCurrent, function (v) {
        saveSetting("schedule_wake_timeout", v);
      }, formatDurationSeconds)
    );
    schedDetails.appendChild(fWakeTimeout);

    schedBody.appendChild(schedDetails);
    return makeCollapsibleCard("Night Schedule", schedBody, true, schedBadge);

  }

  function makeRotationCard() {
    // Rotation
    var rotationBody = el("div");
    var fRotation = field("Rotation");
    var rotationOptions = screenRotationOptionsForUi();
    fRotation.appendChild(
      selectFromOptions(rotationOptions, effectiveScreenRotationForUi(), function (v) {
        saveSetting("screen_rotation", v);
        renderSettings();
      }, function (v) {
        return v + " degrees";
      })
    );
    rotationBody.appendChild(fRotation);
    return makeCollapsibleCard("Rotation", rotationBody, true);

  }

  function makeClockCard() {
    // Clock
    var clockBadge = makeBadge(S.show_clock);
    var clkBody = el("div");
    clkBody.appendChild(toggleSettingRow({
      label: "Show Clock",
      value: S.show_clock,
      getValue: function () { return S.show_clock; },
      setValue: function (value) { S.show_clock = value; },
      badge: clockBadge,
      onChange: function () { saveSetting("show_clock", S.show_clock); }
    }).field);

    var f6 = field("Format");
    f6.appendChild(
      selectFromOptions(productSettingOptions("clock_format"), S.clock_format, function (v) {
        saveSetting("clock_format", v);
      })
    );
    clkBody.appendChild(f6);

    var f7 = field("Timezone");
    f7.appendChild(
      timezoneSelect(S.tz_options, S.timezone, function (v) {
        saveSetting("timezone", v);
      })
    );
    clkBody.appendChild(f7);
    clkBody.appendChild(ntpServersField());
    return makeCollapsibleCard("Clock", clkBody, true, clockBadge);

  }

    function makeFirmwareCard() {
    // Firmware
    var fwBody = el("div", "fw-body");
    var versionLabel = textLabel("Installed", displayVersion(S.firmware || S.installed_version, "Dev"));
    var checkBtn = button("Check for Update", "btn btn-secondary btn-sm");
    var statusMsg = el("span", "fw-status");
    var checkWrap = el("div");
    checkWrap.className = "check-wrap";
    checkWrap.appendChild(statusMsg);
    checkWrap.appendChild(checkBtn);
    var versionBlock = el("div");
    versionBlock.appendChild(actionRow(versionLabel, checkWrap));
    fwBody.appendChild(versionBlock);

    var updatesSection = el("div", "fw-updates");
    var updateRow = el("div");
    updatesSection.appendChild(updateRow);
    fwBody.appendChild(updatesSection);

    function renderUpdateRow() {
      updateRow.innerHTML = "";
      if (!S.update_available) return;
      var label = textLabel("Stable", S.latest_version);
      var installBtn = button("Install", "btn btn-primary btn-sm", function () {
        installBtn.disabled = true;
        installBtn.textContent = "Installing\u2026";
        post(endpoints.update + "/install");
      });
      updateRow.appendChild(actionRow(label, installBtn));
    }

    renderUpdateRow();

    checkBtn.onclick = function () {
      checkBtn.disabled = true;
      checkBtn.textContent = "Checking\u2026";
      statusMsg.textContent = "";
      post(endpoints.firmware_check + "/press")
        .then(function () {
          return new Promise(function (r) {
            setTimeout(r, 4000);
          });
        })
        .then(function () {
          return safeGet(endpoints.update);
        })
        .then(function (data) {
          checkBtn.disabled = false;
          checkBtn.textContent = "Check for Update";
          var hasUpdate = data && data.value &&
            (data.current_version
              ? data.current_version !== data.latest_version
              : data.state === "UPDATE AVAILABLE");
          if (hasUpdate) {
            S.update_available = true;
            S.latest_version = data.latest_version || data.value;
            renderUpdateRow();
          }
          if (!S.update_available) {
            statusMsg.textContent = "Up to date";
            statusMsg.style.color = "var(--success)";
          }
        });
    };

    var autoUpdateOptions = ["Disabled"].concat(productSettingOptions("update_frequency"));
    var currentAutoUpdate = S.auto_update ? S.update_frequency : "Disabled";
    var freqField = field("Auto updates");
    freqField.appendChild(
      selectFromOptions(autoUpdateOptions, currentAutoUpdate, function (v) {
        if (v === "Disabled") {
          saveSetting("auto_update", false);
        } else {
          saveSetting("auto_update", true);
          saveSetting("update_frequency", v);
        }
      })
    );
    fwBody.appendChild(freqField);

    var firmwareUrlStatus = el("div", "status");
    function setFirmwareUrlStatus(msg, ok) {
      setStatus(firmwareUrlStatus, msg, ok ? "green" : "red", ok ? 3000 : null);
    }

    function makeFirmwareUrlField(label, key, placeholder) {
      var f = field(label);
      var firmwareUrlInput = input("url", S[key], placeholder, MAX_FIRMWARE_URL_LENGTH);
      var firmwareUrlError = makeFieldError();
      firmwareUrlInput.onchange = function () {
        var url = normalizeFirmwareManifestUrl(firmwareUrlInput.value);
        firmwareUrlError.textContent = "";
        if (url && !isValidHttpUrl(url)) {
          firmwareUrlError.textContent = "Use a full http:// or https:// URL";
          return;
        }
        saveSetting(key, url)
          .then(function (r) {
            if (!r || !r.ok) throw new Error("save_failed");
            return delayMs(500);
          })
          .then(function () {
            return safeGet(endpoints[key]);
          })
          .then(function (resp) {
            var saved = normalizeFirmwareManifestUrl((resp && (resp.value || resp.state)) || url);
            S[key] = saved;
            firmwareUrlInput.value = saved;
            setFirmwareUrlStatus("Update URL saved", true);
          })
          .catch(function () {
            setFirmwareUrlStatus("Failed to save update URL", false);
          });
      };
      f.appendChild(firmwareUrlInput);
      f.appendChild(firmwareUrlError);
      return f;
    }

    var advancedBody = el("div", "fw-advanced-body");
    var firmwareUrlsHint = el("div", "field-hint");
    firmwareUrlsHint.textContent = "Use a custom manifest to check and install firmware from another location.";
    advancedBody.appendChild(firmwareUrlsHint);
    advancedBody.appendChild(makeFirmwareUrlField(
      "Stable Manifest URL",
      "firmware_manifest_url",
      FIRMWARE_MANIFEST_URLS.stable
    ));
    advancedBody.appendChild(firmwareUrlStatus);

    var advancedPanel = document.createElement("details");
    advancedPanel.className = "inline-expander";
    var advancedSummary = document.createElement("summary");
    advancedSummary.textContent = "Advanced";
    advancedPanel.appendChild(advancedSummary);
    advancedPanel.appendChild(advancedBody);
    fwBody.appendChild(advancedPanel);

    return makeCollapsibleCard("Firmware", fwBody, true);
  }

  function makeWifiCard() {
    var wifiBody = el("div", "fw-body");

    var currentRow = el("div", "field fw-row");
    var currentLabel = el("span", "fw-label");
    currentLabel.innerHTML = '<span style="color:var(--text2)">Current C6 firmware</span> ' +
      esc(displayVersion(S.c6_current_firmware, "Unknown"));
    currentRow.appendChild(currentLabel);
    wifiBody.appendChild(currentRow);

    var availableRow = el("div", "field fw-row");
    var availableLabel = el("span", "fw-label");
    availableLabel.innerHTML = '<span style="color:var(--text2)">Available firmware</span> ' +
      esc(displayVersion(S.c6_available_firmware, "Unknown"));
    var actionWrap = el("div");
    actionWrap.className = "check-wrap";
    var checkBtn = el("button", "btn btn-secondary btn-sm");
    checkBtn.textContent = "Check";
    checkBtn.onclick = function () {
      checkBtn.disabled = true;
      checkBtn.textContent = "Checking\u2026";
      post(endpoints.c6_firmware_check + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            checkBtn.disabled = false;
            checkBtn.textContent = "Check";
          }, 3000);
        });
    };
    var installBtn = el("button", "btn btn-primary btn-sm");
    installBtn.textContent = "Install";
    installBtn.onclick = function () {
      installBtn.disabled = true;
      installBtn.textContent = "Installing\u2026";
      post(endpoints.c6_firmware_install + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            installBtn.disabled = false;
            installBtn.textContent = "Install";
          }, 3000);
        });
    };
    actionWrap.appendChild(checkBtn);
    actionWrap.appendChild(installBtn);
    availableRow.appendChild(availableLabel);
    availableRow.appendChild(actionWrap);
    wifiBody.appendChild(availableRow);

    return makeCollapsibleCard("WiFi", wifiBody, true);
  }

  function makeDeviceRebootCard() {
    var rebootBody = el("div", "fw-body");
    var rebootLabel = textLabel("", "Device Reboot");
    var rebootBtn = button("Reboot Screen", "btn btn-secondary btn-sm", function () {
      rebootBtn.disabled = true;
      rebootBtn.textContent = "Rebooting...";
      post(endpoints.reboot_screen + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            rebootBtn.disabled = false;
            rebootBtn.textContent = "Reboot Screen";
          }, 3000);
        });
    });
    rebootBody.appendChild(actionRow(rebootLabel, rebootBtn));
    return makeCollapsibleCard("Device Reboot", rebootBody, true);
  }

  function makeDeveloperCard() {
    if (!developerPanelEnabledByUrl()) return null;
    var devBadge = makeBadge(S.developer_features_enabled);
    var devBody = el("div");
    devBody.appendChild(toggleSettingRow({
      label: "Enable in-development features",
      value: S.developer_features_enabled,
      getValue: function () { return S.developer_features_enabled; },
      setValue: function (value) { S.developer_features_enabled = value; },
      badge: devBadge,
      onChange: function () {
        saveSetting("developer_features_enabled", S.developer_features_enabled);
        if (!S.developer_features_enabled && isPortraitScreenRotation(S.screen_rotation)) {
          saveSetting("screen_rotation", "0");
        }
        renderSettings();
      }
    }).field);
    return makeCollapsibleCard("Developer", devBody, true, devBadge);
  }

    function appendCards(parent, cards) {
    cards.forEach(function (card) {
      if (card) parent.appendChild(card);
    });
  }

  function settingsCardRenderers() {
    return {
      makeConnectionCard: makeConnectionCard,
      makeFrequencyCard: makeFrequencyCard,
      makePhotoSourceCard: makePhotoSourceCard,
      makeAdvancedFiltersCard: makeAdvancedFiltersCard,
      makeLayoutCard: makeLayoutCard,
      makeMetadataCard: makeMetadataCard,
      makeScreenBrightnessCard: makeScreenBrightnessCard,
      makeScreenToneCard: makeScreenToneCard,
      makeNightScheduleCard: makeNightScheduleCard,
      makeRotationCard: makeRotationCard,
      makeClockCard: makeClockCard,
      makeFirmwareCard: makeFirmwareCard,
      makeWifiCard: makeWifiCard,
      makeDeviceRebootCard: makeDeviceRebootCard,
      makeDeveloperCard: makeDeveloperCard,
      makeBackupCard: makeBackupCard
    };
  }

  function renderSettingsCardsForTab(tabId) {
    var renderers = settingsCardRenderers();
    if (!Array.isArray(WEB_UI_CARDS) || !WEB_UI_CARDS.length) return [];
    return WEB_UI_CARDS.filter(function (card) {
      return card && card.tab === tabId;
    }).map(function (card) {
      var renderer = renderers[card.function];
      return renderer ? renderer() : null;
    });
  }

  function renderSettings() {
    app.innerHTML = "";
    immichApp.innerHTML = "";
    var immichWrap = el("div", "fade-in");
    var wrap = el("div", "fade-in");

    var immichCards = renderSettingsCardsForTab("immich");
    var settingsCards = renderSettingsCardsForTab("settings");
    if (!immichCards.length) immichCards = [
      makeConnectionCard(),
      makeFrequencyCard(),
      makePhotoSourceCard(),
      makeAdvancedFiltersCard(),
      makeLayoutCard(),
      makeMetadataCard()
    ];
    appendCards(immichWrap, immichCards);
    immichApp.appendChild(immichWrap);

    if (!settingsCards.length) settingsCards = [
      makeScreenBrightnessCard(),
      makeScreenToneCard(),
      makeNightScheduleCard(),
      makeRotationCard(),
      makeClockCard(),
      makeFirmwareCard(),
      makeWifiCard(),
      makeDeveloperCard(),
      makeBackupCard(),
      makeDeviceRebootCard()
    ];
    appendCards(wrap, settingsCards);
    app.appendChild(wrap);
  }

    // --- SSE live updates (after render) ---

  function handleLiveEvent(d) {
    if (!d || !d.id) return;
    var id = d.id;
    var stateSpec = ENTITY_STATE_MAP[id];
    if (id === "light/Screen: Backlight") {
      S.backlight_on = d.state === "ON";
      if (d.brightness != null) {
        S.brightness = Math.round((d.brightness / 255) * 100);
        S.brightness_current = S.brightness;
      }
    } else if (id === "switch/Clock: Show") {
      S.show_clock = d.state === "ON" || d.value === true;
    } else if (id === "text_sensor/Screen: Sunrise") {
      S.sunrise = d.value || d.state || "";
      updateSunInfoElement(document.getElementById("sun-info"));
    } else if (id === "text_sensor/Screen: Sunset") {
      S.sunset = d.value || d.state || "";
      updateSunInfoElement(document.getElementById("sun-info"));
    } else if (stateSpec && LIVE_RENDER_STATE_KEYS.indexOf(stateSpec.key) !== -1) {
      applyEntityToState(d);
      if (!isEditingSetting()) renderSettings();
    } else if (stateSpec && liveRenderStateKeyHasPrefix(stateSpec.key)) {
      if (!isEditingSetting()) renderSettings();
    }
  }

  function updateSunInfoElement(el) {
    if (!el) return;
    if (!S.sunrise && !S.sunset) {
      el.style.display = "none";
      return;
    }
    el.style.display = "";
    var t = "";
    if (S.sunrise) t += "Sunrise: " + esc(S.sunrise);
    if (S.sunrise && S.sunset) t += " \u00a0/\u00a0 ";
    if (S.sunset) t += "Sunset: " + esc(S.sunset);
    el.innerHTML = t;
  }

  // --- Hour formatting ---

  function formatHour(h) {
    h = Math.round(h);
    if (h === 0) return "12:00 AM";
    if (h < 12) return h + ":00 AM";
    if (h === 12) return "12:00 PM";
    return (h - 12) + ":00 PM";
  }

  function normalizeScheduleWakeTimeout(value) {
    var seconds = Math.round(Number(value));
    var fallback = PRODUCT_SETTINGS && PRODUCT_SETTINGS.schedule_wake_timeout &&
      PRODUCT_SETTINGS.schedule_wake_timeout.default !== undefined
      ? PRODUCT_SETTINGS.schedule_wake_timeout.default
      : 60;
    var min = productNumberMin("schedule_wake_timeout", 10);
    var max = productNumberMax("schedule_wake_timeout", 3600);
    if (!seconds) seconds = fallback;
    if (seconds < min) seconds = min;
    if (seconds > max) seconds = max;
    return seconds;
  }

  function formatDurationSeconds(seconds) {
    seconds = normalizeScheduleWakeTimeout(seconds);
    if (seconds < 60) return seconds + " seconds";
    if (seconds % 60 === 0) {
      var minutes = seconds / 60;
      return minutes + (minutes === 1 ? " minute" : " minutes");
    }
    return seconds + " seconds";
  }

  // --- Select helpers ---

  function selectFromOptions(options, current, onChange, optionDisplayFn) {
    var display = optionDisplayFn || function (o) { return o; };
    var sel = document.createElement("select");
    sel.className = "select";
    options.forEach(function (o) {
      var opt = document.createElement("option");
      opt.value = o;
      opt.textContent = display(o);
      if (o === current) opt.selected = true;
      sel.appendChild(opt);
    });
    sel.onchange = function () {
      onChange(sel.value);
    };
    return sel;
  }

  function productSelectSettingField(labelText, key, options) {
    var opts = options || {};
    var f = field(labelText);
    var current = opts.current !== undefined ? opts.current : S[key];
    f.appendChild(
      selectFromOptions(productSettingOptions(key, opts.includeDeveloper), current, function (v) {
        saveSetting(key, v);
        if (opts.onChange) opts.onChange(v);
      }, opts.optionDisplayFn)
    );
    return f;
  }

  function segmentedControl(options, current, onChange, optionDisplayFn) {
    var display = optionDisplayFn || function (o) { return o; };
    var seg = el("div", "segment");
    function setActive(value) {
      Array.prototype.forEach.call(seg.children, function (button) {
        var active = button.dataset.value === value;
        button.className = active ? "active" : "";
        button.setAttribute("aria-pressed", active ? "true" : "false");
      });
    }
    options.forEach(function (o) {
      var btn = document.createElement("button");
      btn.type = "button";
      btn.dataset.value = o;
      btn.textContent = display(o);
      btn.setAttribute("aria-pressed", o === current ? "true" : "false");
      btn.onclick = function () {
        setActive(o);
        onChange(o);
      };
      seg.appendChild(btn);
    });
    setActive(current);
    return seg;
  }

  function timezoneSelect(options, current, onChange) {
    current = normalizeTimezoneOption(current);
    return selectFromOptions(options, current, function (v) {
      onChange(normalizeTimezoneOption(v));
    }, function (o) {
      return timezoneDisplayLabel(o);
    });
  }

  function normalizeTimezoneOption(value) {
    if (value === "Asia/Almaty (GMT+6)") return "Asia/Almaty (GMT+5)";
    return value;
  }

  function timezoneDisplayLabel(option) {
    var label = (S.tz_labels && S.tz_labels[option]) || option;
    return label.replace(/_/g, " ");
  }

  // --- Helpers ---

  function el(tag, cls) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    return e;
  }

  function makeBadge(isActive) {
    var badge = el("span", "on-badge" + (isActive ? " active" : ""));
    badge.textContent = "On";
    return badge;
  }

  function setBadgeActive(badge, isActive) {
    if (!badge) return;
    badge.className = "on-badge" + (isActive ? " active" : "");
  }

  function setStatus(target, msg, type, clearAfterMs) {
    if (!target) return;
    target.innerHTML = "";
    if (!msg) {
      target.textContent = "";
      return;
    }
    var dot = el("span", "dot " + (type || "green"));
    target.appendChild(dot);
    target.appendChild(document.createTextNode(" " + msg));
    clearTimeout(target._t);
    if (clearAfterMs) {
      target._t = setTimeout(function () {
        target.textContent = "";
      }, clearAfterMs);
    }
  }

  function button(text, cls, onClick) {
    var btn = el("button", cls || "btn btn-secondary");
    btn.type = "button";
    btn.textContent = text;
    if (onClick) btn.onclick = onClick;
    return btn;
  }

  function actionRow(labelEl, actionEl) {
    var row = el("div", "field fw-row");
    row.appendChild(labelEl);
    row.appendChild(actionEl);
    return row;
  }

  function textLabel(prefix, value) {
    var label = el("span", "fw-label");
    if (prefix) {
      var prefixEl = el("span");
      prefixEl.style.color = "var(--text2)";
      prefixEl.textContent = prefix;
      label.appendChild(prefixEl);
      label.appendChild(document.createTextNode(" " + value));
    } else {
      label.textContent = value;
    }
    return label;
  }

  function toggleSettingRow(options) {
    var opts = options || {};
    var f = field("");
    var row = el("div", "toggle-row");
    var label = el("span");
    label.textContent = opts.label || "";
    var getValue = opts.getValue || function () { return !!opts.value; };
    var setValue = opts.setValue || function (value) { opts.value = value; };
    var toggle = el("div", opts.value ? "toggle on" : "toggle");
    if (opts.disabled) {
      toggle.style.opacity = ".35";
      toggle.style.cursor = "not-allowed";
      if (opts.disabledTitle) toggle.title = opts.disabledTitle;
    }
    toggle.onclick = function () {
      if (opts.disabled) return;
      var next = !getValue();
      setValue(next);
      toggle.className = next ? "toggle on" : "toggle";
      if (opts.details) opts.details.style.display = next ? "" : "none";
      if (opts.badge) setBadgeActive(opts.badge, opts.badgeActive ? opts.badgeActive() : next);
      if (opts.onChange) opts.onChange(next);
    };
    row.appendChild(label);
    row.appendChild(toggle);
    f.appendChild(row);
    return { field: f, toggle: toggle };
  }

  function rangeSettingField(labelText, key, options) {
    var opts = options || {};
    var f = field(labelText || "");
    var rw = el("div", "range-wrap");
    if (opts.leftLabel) {
      var left = el("span", "range-label");
      left.textContent = opts.leftLabel;
      rw.appendChild(left);
    }
    var slider = document.createElement("input");
    slider.type = "range";
    slider.min = productNumberMin(key, opts.minFallback);
    slider.max = productNumberMax(key, opts.maxFallback);
    slider.step = productNumberStep(key, opts.stepFallback);
    slider.value = S[key];
    rw.appendChild(slider);
    if (opts.rightLabel) {
      var right = el("span", "range-label");
      right.textContent = opts.rightLabel;
      rw.appendChild(right);
    }
    if (opts.valueSuffix != null) {
      var value = el("span", "range-val");
      value.textContent = Math.round(S[key]) + opts.valueSuffix;
      slider.oninput = function () {
        value.textContent = slider.value + opts.valueSuffix;
      };
      rw.appendChild(value);
    }
    slider.onchange = function () {
      saveSetting(key, slider.value);
      if (opts.onChange) opts.onChange(slider.value);
    };
    f.appendChild(rw);
    return { field: f, input: slider };
  }

  function hourSelectSettingField(labelText, key) {
    var min = productNumberMin(key, 0);
    var max = productNumberMax(key, 23);
    var options = [];
    for (var h = min; h <= max; h++) options.push(h);
    var f = field(labelText);
    f.appendChild(
      selectFromOptions(options, Math.round(S[key]), function (v) {
        saveSetting(key, parseInt(v));
      }, formatHour)
    );
    return f;
  }

  function makeFieldError() {
    return el("div", "field-error");
  }

  function photoIdListField(options) {
    var opts = options || {};
    var f = field(opts.label);
    var list = el("div", "photo-id-list");
    var idInputs = [];
    var labelInputs = [];
    var error = makeFieldError();
    var removeIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 6h18\"/><path d=\"M8 6V4h8v2\"/><path d=\"M19 6l-1 14H6L5 6\"/><path d=\"M10 11v5\"/><path d=\"M14 11v5\"/></svg>";
    var moveUpIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M12 19V5\"/><path d=\"M5 12l7-7 7 7\"/></svg>";
    var moveDownIcon = "<svg viewBox=\"0 0 24 24\" width=\"18\" height=\"18\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M12 5v14\"/><path d=\"M19 12l-7 7-7-7\"/></svg>";

    function notify(changes, delayMs) {
      if (opts.onChange) opts.onChange(changes, delayMs);
    }

    function refreshRemoveButtons() {
      Array.prototype.forEach.call(list.querySelectorAll(".photo-id-remove"), function (btn) {
        btn.disabled = idInputs.length <= 1;
      });
      Array.prototype.forEach.call(list.querySelectorAll(".photo-id-up"), function (btn) {
        btn.disabled = idInputs.length <= 1 || Number(btn.getAttribute("data-index")) === 0;
      });
      Array.prototype.forEach.call(list.querySelectorAll(".photo-id-down"), function (btn) {
        btn.disabled = idInputs.length <= 1 || Number(btn.getAttribute("data-index")) === idInputs.length - 1;
      });
    }

    function syncMoveButtonIndexes() {
      Array.prototype.forEach.call(list.querySelectorAll(".photo-id-row"), function (row, index) {
        Array.prototype.forEach.call(row.querySelectorAll(".photo-id-up,.photo-id-down"), function (btn) {
          btn.setAttribute("data-index", String(index));
        });
      });
    }

    function moveAlbumIdRow(fromIndex, toIndex) {
      if (toIndex < 0 || toIndex >= idInputs.length || fromIndex === toIndex) return;
      var row = idInputs[fromIndex].closest(".photo-id-row");
      var targetRow = idInputs[toIndex].closest(".photo-id-row");
      var movedInput = idInputs.splice(fromIndex, 1)[0];
      var movedLabel = labelInputs.splice(fromIndex, 1)[0];
      idInputs.splice(toIndex, 0, movedInput);
      labelInputs.splice(toIndex, 0, movedLabel);
      if (toIndex < fromIndex) list.insertBefore(row, targetRow);
      else list.insertBefore(row, targetRow.nextSibling);
      syncMoveButtonIndexes();
      refreshRemoveButtons();
      notify(opts.idChanges, 0);
    }

    function addRow(value, labelValue) {
      var row = el("div", "photo-id-row");
      var fields = el("div", "photo-id-fields");
      var idInput = input("text", value || "", opts.idPlaceholder, MAX_PHOTO_ID_FIELD_LENGTH);
      var labelInput = input("text", labelValue || "", opts.labelPlaceholder, MAX_PHOTO_ID_FIELD_LENGTH);
      var actions = el("div", "photo-id-row-actions");
      if (opts.reorderable) {
        var upBtn = el("button", "btn btn-secondary btn-icon photo-id-up");
        upBtn.type = "button";
        upBtn.innerHTML = moveUpIcon;
        upBtn.title = opts.moveUpTitle;
        upBtn.setAttribute("aria-label", opts.moveUpTitle);
        upBtn.onclick = function () {
          var fromIndex = idInputs.indexOf(idInput);
          moveAlbumIdRow(fromIndex, fromIndex - 1);
        };
        var downBtn = el("button", "btn btn-secondary btn-icon photo-id-down");
        downBtn.type = "button";
        downBtn.innerHTML = moveDownIcon;
        downBtn.title = opts.moveDownTitle;
        downBtn.setAttribute("aria-label", opts.moveDownTitle);
        downBtn.onclick = function () {
          var fromIndex = idInputs.indexOf(idInput);
          moveAlbumIdRow(fromIndex, fromIndex + 1);
        };
        actions.appendChild(upBtn);
        actions.appendChild(downBtn);
      }
      var removeBtn = el("button", "btn btn-secondary btn-icon photo-id-remove");
      removeBtn.type = "button";
      removeBtn.innerHTML = removeIcon;
      removeBtn.title = opts.removeTitle;
      removeBtn.setAttribute("aria-label", opts.removeTitle);
      removeBtn.onclick = function () {
        if (idInputs.length <= 1) {
          idInput.value = "";
          labelInput.value = "";
          notify(opts.clearChanges, 0);
          return;
        }
        var removeIndex = idInputs.indexOf(idInput);
        idInputs.splice(removeIndex, 1);
        labelInputs.splice(removeIndex, 1);
        row.parentNode.removeChild(row);
        syncMoveButtonIndexes();
        refreshRemoveButtons();
        notify(opts.clearChanges, 0);
      };
      idInput.oninput = function () {
        notify(opts.idChanges);
      };
      labelInput.oninput = function () {
        notify(opts.labelChanges);
      };
      fields.appendChild(idInput);
      fields.appendChild(labelInput);
      row.appendChild(fields);
      actions.appendChild(removeBtn);
      row.appendChild(actions);
      list.appendChild(row);
      idInputs.push(idInput);
      labelInputs.push(labelInput);
      syncMoveButtonIndexes();
      refreshRemoveButtons();
    }

    var ids = splitPhotoIdList(S[opts.idKey]);
    var labels = parsePhotoLabelList(S[opts.labelKey]);
    for (var i = 0; i < Math.max(ids.length, labels.length, 1); i++) {
      addRow(ids[i] || "", labels[i] || "");
    }

    var addRowWrap = el("div", "photo-id-actions");
    var addBtn = button(opts.addText, "btn btn-secondary", function () {
      addRow("", "");
      idInputs[idInputs.length - 1].focus();
    });
    addBtn.title = opts.addText;
    addBtn.setAttribute("aria-label", opts.addText);
    addRowWrap.appendChild(addBtn);
    f.appendChild(list);
    f.appendChild(addRowWrap);
    f.appendChild(error);

    return {
      field: f,
      error: error,
      getIdsValue: function () {
        return idInputs.map(function (inputEl) {
          return inputEl.value.trim();
        }).filter(Boolean).join(",");
      },
      getLabelsValue: function () {
        return buildPhotoLabelList(idInputs, labelInputs);
      }
    };
  }

  function makeCollapsibleCard(title, bodyElement, defaultCollapsed, badgeEl) {
    var card = el("div", "card");
    var header = el("div", "card-header");
    var h3 = document.createElement("h3");
    h3.textContent = title;
    var rightWrap = el("div", "card-header-right");
    if (badgeEl) rightWrap.appendChild(badgeEl);
    var chevron = el("span", "card-chevron");
    chevron.innerHTML = "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M6 9l6 6 6-6\"/></svg>";
    rightWrap.appendChild(chevron);
    header.appendChild(h3);
    header.appendChild(rightWrap);
    var body = el("div", "card-body");
    body.appendChild(bodyElement);
    card.appendChild(header);
    card.appendChild(body);
    if (defaultCollapsed) card.classList.add("collapsed");
    header.onclick = function () { card.classList.toggle("collapsed"); };
    return card;
  }

  function makeBackupCard() {
    var backupBody = el("div");
    var backupRow = el("div", "backup-row");
    var exportBtn = el("button", "btn btn-secondary");
    exportBtn.innerHTML = "Export";
    exportBtn.onclick = exportConfig;
    var importBtn = el("button", "btn btn-secondary");
    importBtn.innerHTML = "Import";
    importBtn.onclick = importConfig;
    backupRow.appendChild(exportBtn);
    backupRow.appendChild(importBtn);
    backupBody.appendChild(backupRow);
    return makeCollapsibleCard("Backup", backupBody, true);
  }

  function makeImportSettingsCard() {
    var importBody = el("div");
    var importBtn = el("button", "btn btn-secondary btn-block");
    importBtn.innerHTML = "Import Settings";
    importBtn.onclick = importConfig;
    importBody.appendChild(importBtn);
    return makeCollapsibleCard("Import Settings", importBody, false);
  }

  function field(labelText) {
    var f = el("div", "field");
    if (labelText) {
      var l = document.createElement("label");
      l.textContent = labelText;
      f.appendChild(l);
    }
    return f;
  }

  function ntpServersField() {
    var f = field("NTP Servers");
    var list = el("div", "photo-id-list");
    [
      { key: "ntp_server_1", placeholder: "0.pool.ntp.org", label: "NTP Server 1" },
      { key: "ntp_server_2", placeholder: "1.pool.ntp.org", label: "NTP Server 2" },
      { key: "ntp_server_3", placeholder: "2.pool.ntp.org", label: "NTP Server 3" }
    ].forEach(function (spec) {
      var serverInput = input("text", S[spec.key], spec.placeholder, MAX_NTP_SERVER_LENGTH);
      serverInput.setAttribute("aria-label", spec.label);
      serverInput.onchange = function () {
        saveSetting(spec.key, serverInput.value);
        serverInput.value = S[spec.key];
      };
      list.appendChild(serverInput);
    });
    f.appendChild(list);
    return f;
  }

  function input(type, value, placeholder, maxLength) {
    var i = document.createElement("input");
    i.type = type;
    i.value = value || "";
    if (placeholder) i.placeholder = placeholder;
    if (maxLength != null && maxLength > 0) i.maxLength = maxLength;
    return i;
  }

  function esc(s) {
    var d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }

  // --- Banner ---

  var bannerTimer = null;
  function showBanner(msg, type) {
    if (!els.banner) return;
    els.banner.textContent = msg;
    els.banner.className = "banner banner-" + (type || "success");
    els.banner.style.display = "";
    clearTimeout(bannerTimer);
    bannerTimer = setTimeout(function () { els.banner.style.display = "none"; }, 5000);
  }

    // --- Import / Export ---

  function backupExportFieldValue(entry) {
    if (!entry || !Array.isArray(entry.state_keys) || !entry.state_keys.length) return "";
    if (entry.group === "screen" && entry.field === "schedule_wake_timeout") {
      return normalizeScheduleWakeTimeout(S.schedule_wake_timeout);
    }
    if (entry.state_keys.length > 1) {
      return entry.state_keys.map(function (key) {
        return S[key];
      });
    }
    return S[entry.state_keys[0]];
  }

  function buildBackupExportData() {
    var data = {
      version: BACKUP_CONFIG_VERSION,
      exported_at: new Date().toISOString()
    };
    BACKUP_SCHEMA.forEach(function (entry) {
      if (!entry || !entry.group || !entry.field) return;
      if (!data[entry.group]) data[entry.group] = {};
      data[entry.group][entry.field] = backupExportFieldValue(entry);
    });
    return data;
  }

  var BACKUP_VERSION_MIGRATIONS = {
    1: function backupConfigVersion1(data) {
      return data;
    }
  };

  function validateBackupConfigVersion(data) {
    if (!data || typeof data !== "object" || !Object.prototype.hasOwnProperty.call(data, "version")) {
      return "Invalid config file - missing version";
    }
    if (typeof data.version !== "number" || !isFinite(data.version) || Math.floor(data.version) !== data.version) {
      return "Unsupported backup version " + String(data.version);
    }
    if (data.version > BACKUP_CONFIG_VERSION) {
      return "Unsupported backup version " + data.version + " - this device supports version " + BACKUP_CONFIG_VERSION;
    }
    if (!BACKUP_VERSION_MIGRATIONS[data.version]) {
      return "Unsupported backup version " + data.version;
    }
    return "";
  }

  function migrateBackupConfig(data) {
    return BACKUP_VERSION_MIGRATIONS[data.version](data);
  }

  function exportConfig() {
    var data = buildBackupExportData();
    var json = JSON.stringify(data, null, 2);
    var blob = new Blob([json], { type: "application/json" });
    var url = URL.createObjectURL(blob);
    var now = new Date();
    var name = "espframe-config-" +
      now.getFullYear() + "-" +
      String(now.getMonth() + 1).padStart(2, "0") + "-" +
      String(now.getDate()).padStart(2, "0") + ".json";
    var a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  function backupEntryKey(entry) {
    return entry.group + "." + entry.field;
  }

  function backupImportFieldPresent(data, entry) {
    var groupData = data[entry.group] || {};
    return groupData[entry.field] !== undefined;
  }

  function backupImportFieldValue(data, entry) {
    return (data[entry.group] || {})[entry.field];
  }

  function backupImportStateKey(entry) {
    return entry && Array.isArray(entry.state_keys) && entry.state_keys.length ? entry.state_keys[0] : "";
  }

  var backupImportSavePromises = null;

  function trackBackupImportSave(result) {
    if (backupImportSavePromises) backupImportSavePromises.push(Promise.resolve(result));
  }

  function backupImportEntryUsesPhotoSourceApply(entry) {
    return entry && entry.group === "photos" && Array.isArray(entry.state_keys) &&
      entry.state_keys.some(settingUsesPhotoSourceApply);
  }

  function applyGenericBackupImportField(entry, value) {
    var stateKey = backupImportStateKey(entry);
    if (!stateKey || !endpoints[stateKey]) return false;
    trackBackupImportSave(saveSetting(stateKey, value));
    return true;
  }

  function skipBackupImportField(message) {
    showBanner(message, "error");
    return false;
  }

  function backupImportSummaryMessage(appliedCount, skippedCount) {
    if (!skippedCount) return "Settings imported successfully";
    var skippedText = skippedCount + " skipped " + (skippedCount === 1 ? "setting" : "settings");
    if (appliedCount) return "Imported with " + skippedText;
    return "Import skipped " + skippedCount + " " + (skippedCount === 1 ? "setting" : "settings");
  }

  function applyBackupImportField(entry, value) {
    switch (backupEntryKey(entry)) {
      case "connection.immich_url":
        trackBackupImportSave(saveSetting("immich_url", normalizeImmichUrl(value)));
        return true;
      case "connection.api_key":
        trackBackupImportSave(saveSetting("api_key", value));
        return true;
      case "photos.album_ids":
        var importAlbum = String(value).trim();
        if (photoIdFieldTooLong(importAlbum)) {
          return skipBackupImportField("Album IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importAlbum)) {
          return skipBackupImportField("Import skipped invalid album IDs");
        } else {
          trackBackupImportSave(saveSetting("album_ids", importAlbum));
        }
        return true;
      case "photos.album_labels":
        var importAlbumLabels = String(value).trim();
        if (photoLabelFieldTooLong(importAlbumLabels)) {
          return skipBackupImportField("Album labels exceed 255 characters - not imported");
        } else {
          trackBackupImportSave(saveSetting("album_labels", importAlbumLabels));
        }
        return true;
      case "photos.person_ids":
        var importPerson = String(value).trim();
        if (photoIdFieldTooLong(importPerson)) {
          return skipBackupImportField("Person IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importPerson)) {
          return skipBackupImportField("Import skipped invalid person IDs");
        } else {
          trackBackupImportSave(saveSetting("person_ids", importPerson));
        }
        return true;
      case "photos.person_labels":
        var importPersonLabels = String(value).trim();
        if (photoLabelFieldTooLong(importPersonLabels)) {
          return skipBackupImportField("Person labels exceed 255 characters - not imported");
        } else {
          trackBackupImportSave(saveSetting("person_labels", importPersonLabels));
        }
        return true;
      case "photos.tag_ids":
        var importTag = String(value).trim();
        if (photoIdFieldTooLong(importTag)) {
          return skipBackupImportField("Tag IDs exceed 255 characters - not imported");
        } else if (!isValidUuidList(importTag)) {
          return skipBackupImportField("Import skipped invalid tag IDs");
        } else {
          trackBackupImportSave(saveSetting("tag_ids", importTag));
        }
        return true;
      case "photos.tag_labels":
        var importTagLabels = String(value).trim();
        if (photoLabelFieldTooLong(importTagLabels)) {
          return skipBackupImportField("Tag labels exceed 255 characters - not imported");
        } else {
          trackBackupImportSave(saveSetting("tag_labels", importTagLabels));
        }
        return true;
      case "firmware_updates.manifest_url":
        var importManifestUrl = normalizeFirmwareManifestUrl(value);
        if (importManifestUrl && !isValidHttpUrl(importManifestUrl)) {
          return skipBackupImportField("Stable firmware URL was invalid - not imported");
        } else {
          trackBackupImportSave(saveSetting("firmware_manifest_url", importManifestUrl));
        }
        return true;
      case "clock.timezone":
        trackBackupImportSave(saveSetting("timezone", normalizeTimezoneOption(value)));
        return true;
      case "clock.ntp_servers":
        if (Array.isArray(value)) {
          ["ntp_server_1", "ntp_server_2", "ntp_server_3"].forEach(function (key, idx) {
            if (value[idx] === undefined) return;
            trackBackupImportSave(saveSetting(key, value[idx]));
          });
          return true;
        }
        return skipBackupImportField("NTP servers were invalid - not imported");
      case "screen.schedule_wake_timeout":
        trackBackupImportSave(saveSetting("schedule_wake_timeout", value));
        return true;
      case "screen.rotation":
        var importedRotation = String(value);
        if (screenRotationOptionsForUi().indexOf(importedRotation) !== -1) {
          trackBackupImportSave(saveSetting("screen_rotation", importedRotation));
          return true;
        }
        return skipBackupImportField("Screen rotation was invalid - not imported");
      default:
        return applyGenericBackupImportField(entry, value);
    }
  }

  function importConfig() {
    var fileInput = document.createElement("input");
    fileInput.type = "file";
    fileInput.accept = ".json";
    fileInput.style.display = "none";

    fileInput.addEventListener("change", function () {
      if (!fileInput.files || !fileInput.files[0]) return;
      var reader = new FileReader();
      reader.onload = function () {
        var data;
        try { data = JSON.parse(reader.result); } catch (_) {
          showBanner("Invalid file \u2014 could not parse JSON", "error");
          return;
        }

        var versionError = validateBackupConfigVersion(data);
        if (versionError) {
          showBanner(versionError, "error");
          return;
        }
        data = migrateBackupConfig(data);

        backupImportSavePromises = [];
        var appliedCount = 0;
        var skippedCount = 0;
        var needsPhotoSourceApply = false;
        BACKUP_SCHEMA.forEach(function (entry) {
          if (!backupImportFieldPresent(data, entry)) return;
          if (applyBackupImportField(entry, backupImportFieldValue(data, entry))) {
            appliedCount += 1;
            needsPhotoSourceApply = needsPhotoSourceApply || backupImportEntryUsesPhotoSourceApply(entry);
          } else {
            skippedCount += 1;
          }
        });

        Promise.all(backupImportSavePromises)
          .then(function () {
            if (needsPhotoSourceApply) return post(endpoints.apply_photo_source + "/press");
            return null;
          })
          .then(function () {
            showBanner(backupImportSummaryMessage(appliedCount, skippedCount), skippedCount ? "error" : "success");
            renderSettings();
            backupImportSavePromises = null;
          });
      };
      reader.readAsText(fileInput.files[0]);
    });

    document.body.appendChild(fileInput);
    fileInput.click();
    document.body.removeChild(fileInput);
  }

  // --- Init ---

  buildUI();
  initSSE();
})();
