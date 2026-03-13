#pragma once
/*
 * crypto.hpp — Crypto portfolio tracking for TermiTrade
 *
 * Crypto holdings use fractional amounts (e.g. 0.05 BTC).
 * Prices fetched via Alpha Vantage CURRENCY_EXCHANGE_RATE endpoint,
 * which supports BTC, ETH, BNB, SOL, XRP, DOGE, ADA, etc.
 *
 * All USD values — no fiat switching for now.
 */

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>

// ──────────────────────────────────────────────
//  Supported crypto symbols (for input validation)
// ──────────────────────────────────────────────
static const std::vector<std::string> KNOWN_CRYPTOS = {
    "BTC","ETH","BNB","SOL","XRP","DOGE","ADA","AVAX",
    "MATIC","DOT","LINK","LTC","BCH","ATOM","UNI","NEAR"
};

inline bool is_crypto(const std::string& sym) {
    return std::find(KNOWN_CRYPTOS.begin(), KNOWN_CRYPTOS.end(), sym) != KNOWN_CRYPTOS.end();
}

// ──────────────────────────────────────────────
//  Data structures
// ──────────────────────────────────────────────
struct CryptoTick {
    std::string symbol;
    std::string name;
    double price       = 0.0;
    double change_24h  = 0.0;   // absolute USD
    double change_pct  = 0.0;   // percent
    double high_24h    = 0.0;
    double low_24h     = 0.0;
    double market_cap  = 0.0;   // may be 0 on free tier
    std::vector<double> history;
};

struct CryptoHolding {
    std::string symbol;
    double      amount   = 0.0;   // e.g. 0.05 for 0.05 BTC
    double      avg_cost = 0.0;   // cost per unit in USD
};

struct CryptoPortfolio {
    std::vector<CryptoHolding> holdings;

    void add(const std::string& sym, double amount, double cost) {
        // Merge if already exists
        for (auto& h : holdings) {
            if (h.symbol == sym) {
                double total_cost = h.amount * h.avg_cost + amount * cost;
                h.amount   += amount;
                h.avg_cost  = total_cost / h.amount;
                return;
            }
        }
        holdings.push_back({sym, amount, cost});
    }

    void remove(const std::string& sym) {
        holdings.erase(
            std::remove_if(holdings.begin(), holdings.end(),
                [&](auto& h){ return h.symbol == sym; }),
            holdings.end());
    }
};

// ──────────────────────────────────────────────
//  Formatting helpers for crypto
// ──────────────────────────────────────────────
inline std::string format_crypto_amount(double v) {
    // Show up to 8 decimal places, trim trailing zeros
    std::ostringstream ss;
    if (v >= 100.0)       ss << std::fixed << std::setprecision(2) << v;
    else if (v >= 1.0)    ss << std::fixed << std::setprecision(4) << v;
    else                  ss << std::fixed << std::setprecision(8) << v;
    return ss.str();
}

inline std::string format_usd(double v) {
    std::ostringstream ss;
    if (v >= 1e9)       ss << "$" << std::fixed << std::setprecision(2) << v/1e9  << "B";
    else if (v >= 1e6)  ss << "$" << std::fixed << std::setprecision(2) << v/1e6  << "M";
    else if (v >= 1e3)  ss << "$" << std::fixed << std::setprecision(2) << v/1e3  << "K";
    else                ss << "$" << std::fixed << std::setprecision(2) << v;
    return ss.str();
}

// ──────────────────────────────────────────────
//  Combined watchlist entry (used in main)
// ──────────────────────────────────────────────
enum class AssetType { STOCK_US, STOCK_NSE, STOCK_BSE, CRYPTO };

inline AssetType market_to_type(const std::string& market) {
    if (market == "NSE")    return AssetType::STOCK_NSE;
    if (market == "BSE")    return AssetType::STOCK_BSE;
    if (market == "CRYPTO") return AssetType::CRYPTO;
    return AssetType::STOCK_US;
}

// Converts a symbol + market tag to the API query symbol
// e.g. ("TCS", "NSE") → "TCS.NSE"   ("BTC","CRYPTO") → "BTC"
inline std::string api_symbol(const std::string& sym, AssetType type) {
    switch (type) {
        case AssetType::STOCK_NSE: return sym + ".NSE";
        case AssetType::STOCK_BSE: return sym + ".BSE";
        case AssetType::CRYPTO:    return sym;   // handled by separate endpoint
        default:                   return sym;
    }
}

inline std::string type_badge(AssetType t) {
    switch (t) {
        case AssetType::STOCK_NSE: return "NSE";
        case AssetType::STOCK_BSE: return "BSE";
        case AssetType::CRYPTO:    return "₿  ";
        default:                   return " US";
    }
}
