#pragma once

/* Thu tu moi chuoi: vi, en, zh. Ngon ngu theo cau hinh build Xiaozhi. */
#if defined(CONFIG_LANGUAGE_EN_US) && CONFIG_LANGUAGE_EN_US
#define UI_TR(vi, en, zh) en
#elif (defined(CONFIG_LANGUAGE_ZH_CN) && CONFIG_LANGUAGE_ZH_CN) || \
    (defined(CONFIG_LANGUAGE_ZH_TW) && CONFIG_LANGUAGE_ZH_TW)
#define UI_TR(vi, en, zh) zh
#else
#define UI_TR(vi, en, zh) vi
#endif

#define UI_WIFI UI_TR("Chọn Wi-Fi", "Choose Wi-Fi", "选择网络")
#define UI_SCAN UI_TR("Quét lại", "Scan again", "重新扫描")
#define UI_OTHER UI_TR("Mạng khác", "Other network", "新网络")
#define UI_SCANNING UI_TR("Đang quét mạng...", "Scanning networks...", "正在扫描网络...")
#define UI_NO_AP UI_TR("Không tìm thấy mạng Wi-Fi", "No Wi-Fi networks found", "未找到网络")
#define UI_SCAN_ERROR UI_TR("Không quét được Wi-Fi", "Wi-Fi scan failed", "网络扫描失败")
#define UI_PASSWORD UI_TR("Mật khẩu Wi-Fi", "Wi-Fi password", "网络密码")
#define UI_PASSWORD_MIN_LEN UI_TR("Mật khẩu Wi-Fi, ít nhất 8 ký tự", "Wi-Fi password, at least 8 characters", "Wi-Fi 密码需 8 个")
#define UI_SSID UI_TR("Tên mạng Wi-Fi", "Network name (SSID)", "网络名称")
#define UI_SECURE UI_TR("Có mật khẩu", "Password required", "需要密码")
#define UI_OPEN UI_TR("Mạng mở", "Open network", "开放网络")
#define UI_CONNECT UI_TR("Kết nối", "Connect", "连接")
#define UI_BACK UI_TR("Quay lại", "Back", "回到")
#define UI_CONNECTING UI_TR("Đang kết nối Wi-Fi", "Connecting to Wi-Fi", "正在连接网络")
#define UI_WIFI_ERROR UI_TR("Không kết nối được", "Could not connect", "连接失败")
#define UI_WIFI_HINT UI_TR("Kiểm tra mật khẩu hoặc chọn mạng khác.", "Check the password or choose another network.", "请重输密码或选择新网络")
#define UI_REENTER UI_TR("Nhập lại", "Enter again", "重新输入")
#define UI_TIME UI_TR("Đang đồng bộ ngày giờ", "Synchronizing time", "正在同步时间")
#define UI_TIME_ERROR UI_TR("Chưa đồng bộ được ngày giờ", "Time synchronization failed", "时间同步失败")
#define UI_TIME_HINT UI_TR("Kiểm tra Internet rồi thử lại.", "Check Internet access and try again.", "请连接网络后重试")
#define UI_RETRY UI_TR("Thử lại", "Try again", "重试")
#define UI_CLOUD UI_TR("Kết nối NobleX", "Connect to NobleX", "连接 NobleX")
#define UI_WIFI_OK UI_TR("Wi-Fi đã kết nối", "Wi-Fi connected", "网络已连接")
#define UI_TIME_OK UI_TR("Đã đồng bộ ngày giờ", "Time synchronized", "时间已同步")
#define UI_CLOUD_WAIT UI_TR("Đang kết nối NobleX Cloud...", "Connecting to NobleX Cloud...", "正在连接 NobleX...")
#define UI_GET_QR UI_TR("Lấy mã liên kết", "Get linking code", "取得连接码")
#define UI_SERVER_LOST UI_TR("Mất kết nối máy chủ", "Server connection lost", "服务器连接中断")
#define UI_RECONNECT UI_TR("Đang thử kết nối lại...", "Reconnecting...", "正在重新连接...")
#define UI_LINK_TITLE UI_TR("Liên kết với nhà của bạn", "Link to your home", "连接您的家")
#define UI_LINK_GUIDE UI_TR("Mở app NobleX Smart\nTôi > Quản lý nhà > Home Center > Liên kết", "Open NobleX Smart\nMe > Home management > Home Center > Link", "开启 NobleX Smart\n我的 > 家庭管理 > Home Center > 连接")
#define UI_EXPIRED UI_TR("Mã đã hết hạn", "Code expired", "连接码已过期")
#define UI_DENIED UI_TR("Yêu cầu liên kết đã bị từ chối trên app.", "Linking was declined in the app.", "连接未通过")
#define UI_NEW_QR UI_TR("Tạo mã mới", "Create new code", "取得新连接码")
#define UI_WAIT_QR UI_TR("Chờ mã từ máy chủ...", "Waiting for a linking code...", "等待服务器连接码...")
#define UI_CREATING_QR UI_TR("Đang tạo mã mới...", "Creating a new code...", "正在取得新连接码...")
#define UI_READY UI_TR("Sẵn sàng liên kết", "Ready to link", "可连接")
#define UI_CODE UI_TR("Mã nhập tay: %s", "Manual code: %s", "手动连接码: %s")
#define UI_EXPIRES UI_TR("Hết hạn sau: %02d:%02d", "Expires in: %02d:%02d", "过期时间: %02d:%02d")
#define UI_LINKED UI_TR("Đã liên kết", "Linked", "已连接")
#define UI_SYNC_DATA UI_TR("Đang đồng bộ dữ liệu...", "Synchronizing home data...", "正在同步家庭资料...")
#define UI_COUNTS UI_TR("%u phòng · %u thiết bị · %u cảnh", "%u rooms · %u devices · %u scenes", "%u 房间 · %u 设备 · %u 场景")
#define UI_EMPTY UI_TR("Nhà chưa có dữ liệu", "No home data yet", "家庭暂无资料")
#define UI_ENTER UI_TR("Vào nhà", "Enter home", "回家")
