#pragma once
/*
 * config.hpp — Live-reloadable JSON configuration for TermiTrade
 *
 * Watches config.json for file-modification-time changes and re-applies
 * watchlist / portfolio / alert settings at runtime without restarting.
 *
 * Usage:
 *   ConfigLoader cfg("config.json");
 *   cfg.load();                   // initial load
 *   // ... in background thread ...
 *   if (cfg.has_changed()) cfg.reload();
 */

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <iostream>

using json = nlohmann::json;

// Forward declarations (defined in api_client.hpp)
struct Holding;
struct PriceAlert;
enum class AlertDirection;

// ──────────────────────────────────────────────
//  Raw config data (POD, easy to diff/apply)
// ──────────────────────────────────────────────
struct WatchlistEntry {
    std::string symbol;
    std::string market;   // "US", "NSE", "BSE", "CRYPTO"
};

struct PortfolioEntry {
    std::string symbol;
    double      shares   = 0.0;   // double to support crypto fractions
    double      avg_cost = 0.0;
    std::string asset_type = "stock";  // "stock" | "crypto"
};

struct AlertEntry {
    std::string symbol;
    double      price;
    std::string direction;  // "above" | "below"
};

struct AppConfig {
    std::string             api_key            = "demo";
    int                     refresh_interval   = 15;
    std::string             default_timeframe  = "1D";
    std::string             default_market     = "US";
    std::vector<WatchlistEntry> watchlist;
    std::vector<PortfolioEntry> portfolio;
    std::vector<AlertEntry>     alerts;

    // ── Helpers ──
    bool operator==(const AppConfig& o) const {
        return api_key == o.api_key
            && refresh_interval == o.refresh_interval
            && watchlist.size() == o.watchlist.size()
            && portfolio.size() == o.portfolio.size()
            && alerts.size() == o.alerts.size();
    }
    bool operator!=(const AppConfig& o) const { return !(*this == o); }
};

// ──────────────────────────────────────────────
//  Config loader + file-watcher
// ──────────────────────────────────────────────
class ConfigLoader {
    std::string path_;
    time_t      last_mtime_ = 0;
    AppConfig   current_;

public:
    explicit ConfigLoader(const std::string& path) : path_(path) {}

    // Load (or reload) config from disk; returns true on success
    bool load() {
        std::ifstream f(path_);
        if (!f.is_open()) {
            std::cerr << "[config] Cannot open " << path_ << " — using defaults\n";
            current_ = make_defaults();
            return false;
        }

        try {
            json j;
            f >> j;

            AppConfig cfg;
            cfg.api_key           = j.value("api_key",           "demo");
            cfg.refresh_interval  = j.value("refresh_interval_seconds", 15);
            cfg.default_timeframe = j.value("default_timeframe", "1D");
            cfg.default_market    = j.value("default_market",    "US");

            if (j.contains("watchlist")) {
                for (auto& item : j["watchlist"]) {
                    WatchlistEntry e;
                    if (item.is_string()) {
                        e.symbol = item.get<std::string>();
                        e.market = "US";
                    } else {
                        e.symbol = item.value("symbol", "");
                        e.market = item.value("market", "US");
                    }
                    if (!e.symbol.empty()) cfg.watchlist.push_back(e);
                }
            }

            if (j.contains("portfolio")) {
                for (auto& item : j["portfolio"]) {
                    PortfolioEntry e;
                    e.symbol     = item.value("symbol",     "");
                    e.shares     = item.value("shares",     0.0);
                    e.avg_cost   = item.value("avg_cost",   0.0);
                    e.asset_type = item.value("asset_type", "stock");
                    if (!e.symbol.empty()) cfg.portfolio.push_back(e);
                }
            }

            if (j.contains("alerts")) {
                for (auto& item : j["alerts"]) {
                    AlertEntry e;
                    e.symbol    = item.value("symbol",    "");
                    e.price     = item.value("price",     0.0);
                    e.direction = item.value("direction", "above");
                    if (!e.symbol.empty()) cfg.alerts.push_back(e);
                }
            }

            current_    = cfg;
            last_mtime_ = get_mtime();
            return true;

        } catch (const std::exception& ex) {
            std::cerr << "[config] Parse error: " << ex.what() << "\n";
            current_ = make_defaults();
            return false;
        }
    }

    // Returns true if the file on disk is newer than last load
    bool has_changed() const {
        return get_mtime() > last_mtime_;
    }

    // Reload if changed; returns true if config was actually updated
    bool reload_if_changed() {
        if (!has_changed()) return false;
        AppConfig prev = current_;
        load();
        return current_ != prev;
    }

    const AppConfig& get() const { return current_; }

    // Write a blank template config.json (handy on first run)
    static void write_example(const std::string& path) {
        json j = {
            {"api_key",                    "YOUR_KEY_HERE"},
            {"refresh_interval_seconds",   15},
            {"default_timeframe",          "1D"},
            {"default_market",             "US"},
            {"watchlist", json::array({
                {{"symbol","AAPL"},   {"market","US"}},
                {{"symbol","NVDA"},   {"market","US"}},
                {{"symbol","TCS"},    {"market","NSE"}},
                {{"symbol","BTC"},    {"market","CRYPTO"}},
            })},
            {"portfolio", json::array({
                {{"symbol","AAPL"}, {"shares",10},    {"avg_cost",160.0}, {"asset_type","stock"}},
                {{"symbol","TCS"},  {"shares",5},     {"avg_cost",3400.0},{"asset_type","stock"}},
                {{"symbol","BTC"},  {"shares",0.05},  {"avg_cost",42000.0},{"asset_type","crypto"}},
            })},
            {"alerts", json::array({
                {{"symbol","AAPL"}, {"price",190.0}, {"direction","above"}},
                {{"symbol","TSLA"}, {"price",240.0}, {"direction","below"}},
            })}
        };
        std::ofstream f(path);
        f << j.dump(2);
    }

private:
    time_t get_mtime() const {
        struct stat st{};
        if (stat(path_.c_str(), &st) == 0) return st.st_mtime;
        return 0;
    }

    static AppConfig make_defaults() {
        AppConfig cfg;
        cfg.watchlist = {
            {"AAPL","US"}, {"GOOGL","US"}, {"MSFT","US"},
            {"TSLA","US"}, {"NVDA","US"},  {"META","US"}
        };
        cfg.portfolio = {
            {"AAPL",  10,  160.0, "stock"},
            {"NVDA",   5,  580.0, "stock"},
            {"MSFT",   8,  350.0, "stock"},
            {"TSLA",  15,  220.0, "stock"},
        };
        cfg.alerts = {
            {"AAPL", 190.0, "above"},
            {"TSLA", 240.0, "below"},
        };
        return cfg;
    }
};
