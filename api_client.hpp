#pragma once
/*
 * api_client.hpp — Alpha Vantage API wrapper for TermiTrade
 * Handles HTTP requests via libcurl, parses JSON responses
 */

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

// libcurl write callback
static size_t write_cb(void* data, size_t size, size_t nmemb, std::string* out) {
    out->append((char*)data, size * nmemb);
    return size * nmemb;
}

class ApiClient {
    std::string api_key_;
    CURL* curl_;

public:
    ApiClient(const std::string& key) : api_key_(key) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ = curl_easy_init();
    }
    ~ApiClient() {
        curl_easy_cleanup(curl_);
        curl_global_cleanup();
    }

    // Fetch real-time quote from Alpha Vantage
    // Returns: { price, change, change_pct, volume, open, high, low }
    json fetch_quote(const std::string& symbol) {
        std::string url = "https://www.alphavantage.co/query?function=GLOBAL_QUOTE"
                          "&symbol=" + symbol + "&apikey=" + api_key_;
        std::string resp = http_get(url);
        try {
            auto j = json::parse(resp);
            auto& q = j["Global Quote"];
            return {
                {"price",      std::stod(q["05. price"].get<std::string>())},
                {"change",     std::stod(q["09. change"].get<std::string>())},
                {"change_pct", std::stod(q["10. change percent"].get<std::string>())},
                {"volume",     q["06. volume"].get<std::string>()},
                {"open",       std::stod(q["02. open"].get<std::string>())},
                {"high",       std::stod(q["03. high"].get<std::string>())},
                {"low",        std::stod(q["04. low"].get<std::string>())},
            };
        } catch (...) {
            return {};  // Demo mode or API error
        }
    }

    // Fetch historical prices (daily close) for chart
    std::vector<double> fetch_history(const std::string& symbol, const std::string& tf) {
        std::string func = (tf == "1D") ? "TIME_SERIES_INTRADAY&interval=5min"
                                        : "TIME_SERIES_DAILY";
        std::string url = "https://www.alphavantage.co/query?function=" + func
                        + "&symbol=" + symbol + "&apikey=" + api_key_ + "&outputsize=compact";
        std::string resp = http_get(url);
        std::vector<double> prices;
        try {
            auto j = json::parse(resp);
            std::string key = (tf == "1D") ? "Time Series (5min)" : "Time Series (Daily)";
            auto& ts = j[key];
            // Collect and reverse (oldest first)
            for (auto it = ts.begin(); it != ts.end(); ++it)
                prices.push_back(std::stod(it.value()["4. close"].get<std::string>()));
            std::reverse(prices.begin(), prices.end());
            // Trim to reasonable length
            int limit = (tf=="1D")?78 : (tf=="5D")?130 : (tf=="1M")?30 : (tf=="3M")?90 : 252;
            if ((int)prices.size() > limit) prices = {prices.end()-limit, prices.end()};
        } catch (...) {
            // Return demo sine-wave data if API unavailable
            prices = demo_prices(80);
        }
        return prices;
    }

private:
    std::string http_get(const std::string& url) {
        std::string response;
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
        curl_easy_perform(curl_);
        return response;
    }

    std::vector<double> demo_prices(int n) {
        std::vector<double> v;
        double p = 150.0;
        for (int i = 0; i < n; i++) {
            p += (rand() % 100 - 48) * 0.05;
            v.push_back(p);
        }
        return v;
    }
};


// ════════════════════════════════════════════════
//  watchlist.hpp — Stock data structures
// ════════════════════════════════════════════════
struct Stock {
    std::string symbol;
    std::string name;
    double price      = 0.0;
    double change     = 0.0;
    double change_pct = 0.0;
    double open = 0.0, high = 0.0, low = 0.0;
    std::string volume_str;
    long long volume  = 0;
    std::vector<double> history;
};

struct Watchlist {
    std::vector<Stock> stocks;
    void add(const std::string& sym, const std::string& name) {
        stocks.push_back({sym, name});
    }
};


// ════════════════════════════════════════════════
//  portfolio.hpp — Holdings & P&L
// ════════════════════════════════════════════════
struct Holding {
    std::string symbol;
    int    shares;
    double avg_cost;
};

struct Portfolio {
    std::vector<Holding> holdings;
    void add(const std::string& sym, int shares, double cost) {
        holdings.push_back({sym, shares, cost});
    }
};


// ════════════════════════════════════════════════
//  alerts.hpp — Price alert system
// ════════════════════════════════════════════════
enum class AlertType      { UP, DOWN, SYS };
enum class AlertDirection { ABOVE, BELOW };

struct Alert {
    AlertType   type;
    std::string message;
    std::string timestamp;
};

struct PriceAlert {
    std::string     symbol;
    double          threshold;
    AlertDirection  direction;
    bool            triggered = false;
};

class AlertSystem {
    std::vector<Alert>      log_;
    std::vector<PriceAlert> rules_;

public:
    void add_price_alert(const std::string& sym, double price, AlertDirection dir) {
        rules_.push_back({sym, price, dir});
    }

    void check_alerts(const std::vector<Stock>& stocks) {
        for (auto& rule : rules_) {
            if (rule.triggered) continue;
            auto it = std::find_if(stocks.begin(), stocks.end(),
                [&](auto& s){ return s.symbol == rule.symbol; });
            if (it == stocks.end()) continue;

            bool hit = (rule.direction == AlertDirection::ABOVE && it->price >= rule.threshold)
                    || (rule.direction == AlertDirection::BELOW && it->price <= rule.threshold);
            if (hit) {
                std::string dir_str = rule.direction == AlertDirection::ABOVE ? "crossed above" : "dropped below";
                push(AlertType::UP, rule.symbol + " " + dir_str + " $" + format_thresh(rule.threshold));
                rule.triggered = true;
            }
        }
    }

    void push(AlertType t, const std::string& msg) {
        auto now = std::time(nullptr);
        std::tm* lt = std::localtime(&now);
        char buf[10];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", lt);
        log_.insert(log_.begin(), {t, msg, buf});
        if (log_.size() > 50) log_.pop_back();
    }

    std::vector<Alert> get_recent(int n) const {
        return {log_.begin(), log_.begin() + std::min((int)log_.size(), n)};
    }

private:
    static std::string format_thresh(double v) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << v;
        return ss.str();
    }
};


// ════════════════════════════════════════════════
//  utils — Shared formatting helpers
// ════════════════════════════════════════════════
inline std::string format_price(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
}

inline std::string format_pct(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
}

inline std::string get_time_str() {
    auto now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", lt);
    return buf;
}
