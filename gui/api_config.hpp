#ifndef API_CONFIG_HPP
#define API_CONFIG_HPP

/** 后端 API 根地址（无末尾斜杠）。部署时只需改此处或改 CMake 注入宏。 */
inline constexpr char kKeyApiBaseUrl[] = "https://fish.huaihaizhonlu.xyz";

/** 登录路径相对后缀：最终为 base + "fish/key/pc/" + percentEncode(key)（PC 端 /fish/key/pc/:key）。 */
inline constexpr char kKeyLoginRelative[] = "fish/key/pc";

/** 心跳路径相对后缀：最终为 base 解析后的 "fish/keybeats"（POST + Authorization）。 */
inline constexpr char kKeyBeatsRelative[] = "fish/keybeats";

/** 登出路径相对后缀：GET + Authorization（会话 key）。 */
inline constexpr char kKeyLogoutRelative[] = "fish/keylogout";

/** 心跳间隔（毫秒）。 */
inline constexpr int kKeyHeartbeatIntervalMs = 15000;

/** 登录/单次心跳请求超时（毫秒）。 */
inline constexpr int kKeyHttpTimeoutMs = 30000;

#endif
