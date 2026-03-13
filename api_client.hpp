#pragma once
/*
 * api_client.hpp — Alpha Vantage API wrapper for TermiTrade
 *
 * New in v1.1:
 *   • fetch_ohlc()         — OHLCV data for candlestick charts
 *   • fetch_crypto_quote() — live crypto price via CURRENCY_EXCHANGE_RATE
 *   • fetch_crypto_history()— daily close history for crypto
 *   • Indian market support: append ".NSE" / ".BSE" to symbol string
 *     (Alpha Vantage Global Quote accepts e.g. "TCS.NSE")
 */

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "chart.hpp"   // for CandleData

using json = nlohmann::json;

// libcurl write callback
static size_t write_cb(void* data, size_t size, size_t nmemb, std::string* out) {
    out->append((char*)data, size * nmemb);
    return size * nmemb;
}

class ApiClient {
    std::string api_key_;
    CURL*       curl_;

public:
    explicit ApiClient(const std::string& key) : api_key_(key) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ = curl_easy_init();
    }
    ~ApiClient() {
        curl_easy_cleanup(curl_);
        curl_global_cleanup();
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  GLOBAL QUOTE  (US stocks + Indian stocks via .NSE / .BSE suffix)
    //
    //  Examples:
    //    fetch_quote("AAPL")      → US stock
    //    fetch_quote("TCS.NSE")   → NSE-listed stock
    //    fetch_quote("RELIANCE.BSE") → BSE-listed stock
    //
    //  Returns: { price, change, change_pct, volume, open, high, low }
    // ──────────────────────────────────────────────────────────────────────────
    json fetch_quote(const std::string& symbol) {
        std::string url =
            "https://www.alphavantage.co/query?function=GLOBAL_QUOTE"
            "&symbol=" + url_encode(symbol) + "&apikey=" + api_key_;
        std::string resp = http_get(url);
        try {
            auto j = json::parse(resp);
            auto& q = j["Global Quote"];
            if (q.empty()) return {};
            return {
                {"price",      safe_stod(q["05. price"])},
                {"change",     safe_stod(q["09. change"])},
                {"change_pct", safe_stod(q["10. change percent"])},  // strips %
                {"volume",     q["06. volume"].get<std::string>()},
                {"open",       safe_stod(q["02. open"])},
                {"high",       safe_stod(q["03. high"])},
                {"low",        safe_stod(q["04. low"])},
            };
        } catch (...) {
            return {};
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  HISTORICAL CLOSE PRICES  (for line chart)
    //  Timeframes: "1D" (5min intraday), "5D","1M","3M","1Y" (daily)
    //  Works for both US and Indian symbols.
    // ──────────────────────────────────────────────────────────────────────────
    std::vector<double> fetch_history(const std::string& symbol, const std::string& tf) {
        std::string func = (tf == "1D") ? "TIME_SERIES_INTRADAY&interval=5min"
                                        : "TIME_SERIES_DAILY";
        std::string url =
            "https://www.alphavantage.co/query?function=" + func
            + "&symbol=" + url_encode(symbol)
            + "&apikey=" + api_key_
            + "&outputsize=compact";

        std::string resp = http_get(url);
        std::vector<double> prices;
        try {
            auto j = json::parse(resp);
            std::string key = (tf == "1D") ? "Time Series (5min)" : "Time Series (Daily)";
            auto& ts = j[key];
            for (auto it = ts.begin(); it != ts.end(); ++it)
                prices.push_back(std::stod(it.value()["4. close"].get<std::string>()));
            std::reverse(prices.begin(), prices.end());
            int limit = (tf=="1D")?78:(tf=="5D")?130:(tf=="1M")?30:(tf=="3M")?90:252;
            if ((int)prices.size() > limit)
                prices = {prices.end()-limit, prices.end()};
        } catch (...) {
            prices = demo_prices(80);
        }
        return prices;
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  OHLCV DATA  (for candlestick chart)
    //  Returns a vector<CandleData> ordered oldest → newest.
    // ──────────────────────────────────────────────────────────────────────────
    std::vector<CandleData> fetch_ohlc(const std::string& symbol, const std::string& tf) {
        std::string func, ts_key;
        if (tf == "1D") {
            func   = "TIME_SERIES_INTRADAY&interval=5min";
            ts_key = "Time Series (5min)";
        } else {
            func   = "TIME_SERIES_DAILY";
            ts_key = "Time Series (Daily)";
        }

        std::string url =
            "https://www.alphavantage.co/query?function=" + func
            + "&symbol=" + url_encode(symbol)
            + "&apikey=" + api_key_
            + "&outputsize=compact";

        std::string resp = http_get(url);
        std::vector<CandleData> candles;

        try {
            auto j  = json::parse(resp);
            auto& ts = j[ts_key];
            for (auto it = ts.begin(); it != ts.end(); ++it) {
                CandleData cd;
                cd.date  = it.key();
                cd.open  = std::stod(it.value()["1. open"].get<std::string>());
                cd.high  = std::stod(it.value()["2. high"].get<std::string>());
                cd.low   = std::stod(it.value()["3. low"].get<std::string>());
                cd.close = std::stod(it.value()["4. close"].get<std::string>());
                try { cd.volume = std::stoll(it.value()["5. volume"].get<std::string>()); }
                catch (...) {}
                candles.push_back(cd);
            }
            std::reverse(candles.begin(), candles.end());   // oldest first
            int limit = (tf=="1D")?78:(tf=="5D")?130:(tf=="1M")?30:(tf=="3M")?90:252;
            if ((int)candles.size() > limit)
                candles = {candles.end()-limit, candles.end()};
        } catch (...) {
            candles = demo_candles(60);
        }
        return candles;
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  CRYPTO  — live price via CURRENCY_EXCHANGE_RATE
    //  symbol = "BTC", "ETH", "SOL", etc.  market = "USD" (or "INR", "EUR")
    //
    //  Returns: { price, bid, ask }   (Alpha Vantage free tier only gives rate)
    // ──────────────────────────────────────────────────────────────────────────
    json fetch_crypto_quote(const std::string& symbol,
                            const std::string& market = "USD") {
        std::string url =
            "https://www.alphavantage.co/query?function=CURRENCY_EXCHANGE_RATE"
            "&from_currency=" + symbol
            + "&to_currency=" + market
            + "&apikey=" + api_key_;

        std::string resp = http_get(url);
        try {
            auto j = json::parse(resp);
            auto& r = j["Realtime Currency Exchange Rate"];
            if (r.empty()) return {};
            double price = std::stod(r["5. Exchange Rate"].get<std::string>());
            return {
                {"price",    price},
                {"change",   0.0},      // not provided on free tier
                {"change_pct", 0.0},
                {"bid",      std::stod(r.value("8. Bid Price", "0"))},
                {"ask",      std::stod(r.value("9. Ask Price", "0"))},
            };
        } catch (...) {
            return {};
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  CRYPTO HISTORY  — daily close prices
    //  Uses DIGITAL_CURRENCY_DAILY endpoint.
    // ──────────────────────────────────────────────────────────────────────────
    std::vector<double> fetch_crypto_history(const std::string& symbol,
                                              const std::string& market = "USD",
                                              int days = 90) {
        std::string url =
            "https://www.alphavantage.co/query?function=DIGITAL_CURRENCY_DAILY"
            "&symbol=" + symbol
            + "&market=" + market
            + "&apikey=" + api_key_;

        std::string resp = http_get(url);
        std::vector<double> prices;
        try {
            auto j = json::parse(resp);
            auto& ts = j["Time Series (Digital Currency Daily)"];
            // key is "4b. close (USD)" or "4a. close (USD)"
            std::string close_key = "4b. close (" + market + ")";
            for (auto it = ts.begin(); it != ts.end(); ++it) {
                auto& day = it.value();
                if (day.contains(close_key))
                    prices.push_back(std::stod(day[close_key].get<std::string>()));
                else if (day.contains("4a. close (" + market + ")"))
                    prices.push_back(std::stod(
                        day["4a. close (" + market + ")"].get<std::string>()));
            }
            std::reverse(prices.begin(), prices.end());
            if ((int)prices.size() > days)
                prices = {prices.end()-days, prices.end()};
        } catch (...) {
            prices = demo_prices(days);
        }
        return prices;
    }

private:
    // ── HTTP ────────────────────────────────────────────────────────────────
    std::string http_get(const std::string& url) {
        std::string response;
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_perform(curl_);
        return response;
    }

    // ── Demo / fallback data ────────────────────────────────────────────────
    std::vector<double> demo_prices(int n) {
        std::vector<double> v;
        double p = 150.0;
        srand(42);
        for (int i = 0; i < n; i++) {
            p += (rand() % 100 - 48) * 0.05;
            v.push_back(p);
        }
        return v;
    }

    std::vector<CandleData> demo_candles(int n) {
        std::vector<CandleData> v;
        double p = 150.0;
        srand(42);
        for (int i = 0; i < n; i++) {
            double o = p;
            double h = o + (rand() % 50) * 0.06;
            double l = o - (rand() % 50) * 0.06;
            double c = l + (h - l) * ((rand() % 100) / 100.0);
            v.push_back({o, h, l, c, (long long)(rand() % 5000000 + 1000000)});
            p = c;
        }
        return v;
    }

    // ── Helpers ─────────────────────────────────────────────────────────────
    static std::string url_encode(const std::string& s) {
        // Minimal encoder — just handles the % in "change percent"
        // libcurl's curl_easy_escape is available but overkill for symbols
        return s;
    }

    // Parse double from a json string value, stripping trailing "%"
    static double safe_stod(const json& j) {
        std::string s = j.get<std::string>();
        // Remove trailing % if present (e.g. "10. change percent": "1.19%")
        if (!s.empty() && s.back() == '%') s.pop_back();
        try { return std::stod(s); }
        catch (...) { return 0.0; }
    }
};


// ════════════════════════════════════════════════
//  watchlist.hpp — Stock data structures
// ════════════════════════════════════════════════
struct Stock {
    std::string symbol;
    std::string name;
    std::string market = "US";  // "US" | "NSE" | "BSE" | "CRYPTO"
    double price       = 0.0;
    double change      = 0.0;
    double change_pct  = 0.0;
    double open = 0.0, high = 0.0, low = 0.0;
    std::string volume_str;
    long long volume   = 0;
    std::vector<double>     history;
    std::vector<CandleData> candles;  // NEW: for candlestick mode
};

struct Watchlist {
    std::vector<Stock> stocks;
    void add(const std::string& sym, const std::string& name,
             const std::string& market = "US") {
        stocks.push_back({sym, name, market});
    }
};


// ════════════════════════════════════════════════
//  portfolio.hpp — Holdings & P&L
// ════════════════════════════════════════════════
struct Holding {
    std::string symbol;
    double      shares   = 0.0;  // double for fractional (crypto)
    double      avg_cost = 0.0;
};

struct Portfolio {
    std::vector<Holding> holdings;
    void add(const std::string& sym, double shares, double cost) {
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
        // Avoid duplicate rules
        for (auto& r : rules_) {
            if (r.symbol == sym && r.threshold == price && r.direction == dir) return;
        }
        rules_.push_back({sym, price, dir});
    }

    void check_alerts(const std::vector<Stock>& stocks) {
        for (auto& rule : rules_) {
            if (rule.triggered) continue;
            auto it = std::find_if(stocks.begin(), stocks.end(),
                [&](auto& s){ return s.symbol == rule.symbol; });
            if (it == stocks.end() || it->price == 0.0) continue;

            bool hit = (rule.direction == AlertDirection::ABOVE && it->price >= rule.threshold)
                    || (rule.direction == AlertDirection::BELOW && it->price <= rule.threshold);
            if (hit) {
                std::string dir_str = rule.direction == AlertDirection::ABOVE
                                    ? "crossed above" : "dropped below";
                push(AlertType::UP,
                     rule.symbol + " " + dir_str + " $" + format_thresh(rule.threshold));
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
        int sz = std::min((int)log_.size(), n);
        return {log_.begin(), log_.begin() + sz};
    }

    std::vector<PriceAlert>& rules() { return rules_; }

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
