#include "config_io.h"
#include "third_party/nlohmann/json.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <Windows.h>

using json = nlohmann::json;

namespace config_io {

// 把 float 舍入到 6 位有效小数, 避免 JSON 输出 0.20000000298023224 之类的经典浮点问题
// 原理: float → double 时精度"膨胀", dump() 如实写出完整 double → 又臭又长
//       舍入到 double 层级后 dump() 会自动选最短唯一表示字符串
static double Round6(float v) {
    return std::round(static_cast<double>(v) * 1000000.0) / 1000000.0;
}

std::filesystem::path GetExeDirectory() {
    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::filesystem::current_path();
    }
    std::filesystem::path p(exePath);
    return p.parent_path();
}

std::filesystem::path GetExeConfigPath(const wchar_t* fileName) {
    return GetExeDirectory() / fileName;
}

std::string ToJsonString(const Config& cfg) {
    json j;
    j[key::Version]   = 1;
    j[key::Note]      = "Myopic Defocus 配置 - 修改后保存即可生效";
    j[key::DiagInch]  = Round6(cfg.diagInch);
    j[key::ScreenDistanceCM] = Round6(cfg.screenDistanceCM);
    j[key::PupilSizeUm]      = Round6(cfg.pupilSizeUm);
    j[key::EffectStrength]   = Round6(cfg.effectStrength);

    if (cfg.resX > 0.0f && cfg.resY > 0.0f) {
        j[key::ResX] = Round6(cfg.resX);
        j[key::ResY] = Round6(cfg.resY);
    } else {
        j[key::ResX] = nullptr;
        j[key::ResY] = nullptr;
    }

    j[key::TargetFps] = cfg.targetFps;
    return j.dump(4);
}

bool FromJsonString(const std::string& jsonStr, Config& cfg) {
    try {
        json j = json::parse(jsonStr);

        if (j.contains(key::DiagInch) && !j[key::DiagInch].is_null())
            cfg.diagInch = j[key::DiagInch].get<float>();
        if (j.contains(key::ScreenDistanceCM) && !j[key::ScreenDistanceCM].is_null())
            cfg.screenDistanceCM = j[key::ScreenDistanceCM].get<float>();
        if (j.contains(key::PupilSizeUm) && !j[key::PupilSizeUm].is_null())
            cfg.pupilSizeUm = j[key::PupilSizeUm].get<float>();
        if (j.contains(key::EffectStrength) && !j[key::EffectStrength].is_null())
            cfg.effectStrength = j[key::EffectStrength].get<float>();
        if (j.contains(key::ResX) && !j[key::ResX].is_null())
            cfg.resX = j[key::ResX].get<float>();
        if (j.contains(key::ResY) && !j[key::ResY].is_null())
            cfg.resY = j[key::ResY].get<float>();
        if (j.contains(key::TargetFps) && !j[key::TargetFps].is_null())
            cfg.targetFps = j[key::TargetFps].get<int>();

        return true;
    } catch (const json::exception& e) {
        std::string msg = e.what();
        std::cerr << "[config_io] JSON error: " << msg << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::string msg = e.what();
        std::cerr << "[config_io] Error: " << msg << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[config_io] Unknown exception" << std::endl;
        return false;
    }
}

bool SaveToExeDir(const wchar_t* fileName, const Config& cfg) {
    std::filesystem::path path = GetExeConfigPath(fileName);
    try {
        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            std::cerr << "[config_io] Failed to open for write: " << path << std::endl;
            return false;
        }
        ofs << ToJsonString(cfg);
        ofs.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[config_io] Save exception: " << e.what() << std::endl;
        return false;
    }
}

bool LoadFromExeDir(const wchar_t* fileName, Config& cfg, bool& loaded) {
    std::filesystem::path path = GetExeConfigPath(fileName);

    if (!std::filesystem::exists(path)) {
        loaded = false;
        return true;
    }

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            std::cerr << "[config_io] Failed to open: " << path << std::endl;
            loaded = false;
            return false;
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string content = ss.str();

        if (FromJsonString(content, cfg)) {
            loaded = true;
            return true;
        }
        loaded = false;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[config_io] Load exception: " << e.what() << std::endl;
        loaded = false;
        return false;
    }
}

} // namespace config_io