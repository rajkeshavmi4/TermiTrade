#pragma once
/*
 * export.hpp — Portfolio CSV export for TermiTrade
 *
 * Exports combined stock + crypto holdings to a timestamped CSV file.
 *
 * Output columns:
 *   Symbol, Market, Type, Units, AvgCost, CurrentPrice,
 *   CurrentValue, CostBasis, PnL, PnL%, LastUpdated
 *
 * Usage:
 *   std::string path = PortfolioExporter::export_csv(
 *       g_watchlist, g_portfolio, g_crypto_portfolio, g_crypto_ticks);
 *   // path = "termitrade_portfolio_20250313_143021.csv"
 */

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <stdexcept>

// Forward declarations
struct Stock;
struct Watchlist;
struct Holding;
struct Portfolio;
struct CryptoHolding;
struct CryptoPortfolio;
struct CryptoTick;

// ──────────────────────────────────────────────
//  CSV cell helpers
// ──────────────────────────────────────────────
static std::string csv_str(const std::string& s) {
    // Wrap in quotes if it contains a comma or quote
    if (s.find(',') != std::string::npos || s.find('"') != std::string::npos) {
        std::string out = "\"";
        for (char c : s) { if (c == '"') out += '"'; out += c; }
        out += '"';
        return out;
    }
    return s;
}

static std::string csv_dbl(double v, int prec = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << v;
    return ss.str();
}

static std::string now_timestamp() {
    auto t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", lt);
    return buf;
}

static std::string now_iso() {
    auto t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt);
    return buf;
}

// ──────────────────────────────────────────────
//  Exporter
// ──────────────────────────────────────────────
class PortfolioExporter {
public:
    /*
     * export_csv()  — writes everything to a CSV and returns the filename.
     * Throws std::runtime_error if the file cannot be written.
     */
    static std::string export_csv(
        const std::vector<Stock>&        stocks,        // watchlist data (has live prices)
        const std::vector<Holding>&      stock_holds,   // stock holdings
        const std::vector<CryptoTick>&   crypto_ticks,  // live crypto prices
        const std::vector<CryptoHolding>& crypto_holds,  // crypto holdings
        const std::string& output_dir = ".")
    {
        std::string filename = output_dir + "/termitrade_portfolio_" + now_timestamp() + ".csv";
        std::ofstream f(filename);
        if (!f.is_open())
            throw std::runtime_error("Cannot open " + filename + " for writing");

        // Header
        f << "Symbol,Market,Type,Units,AvgCostUSD,CurrentPriceUSD,"
          << "CurrentValueUSD,CostBasisUSD,PnLUSD,PnLPct,LastUpdated\n";

        double grand_value = 0, grand_cost = 0, grand_pnl = 0;

        // ── Stock holdings ──
        for (auto& h : stock_holds) {
            // Find live price from watchlist
            double cur_price = 0.0;
            std::string market = "US";
            for (auto& s : stocks) {
                if (s.symbol == h.symbol) {
                    cur_price = s.price;
                    break;
                }
            }
            double cur_value  = cur_price * h.shares;
            double cost_basis = h.avg_cost * h.shares;
            double pnl        = cur_value - cost_basis;
            double pnl_pct    = cost_basis > 0 ? (pnl / cost_basis) * 100.0 : 0.0;

            grand_value += cur_value;
            grand_cost  += cost_basis;
            grand_pnl   += pnl;

            f << csv_str(h.symbol)            << ","
              << csv_str(market)              << ","
              << "Stock"                      << ","
              << csv_dbl(h.shares, 0)         << ","
              << csv_dbl(h.avg_cost)          << ","
              << csv_dbl(cur_price)           << ","
              << csv_dbl(cur_value)           << ","
              << csv_dbl(cost_basis)          << ","
              << csv_dbl(pnl)                 << ","
              << csv_dbl(pnl_pct)             << ","
              << now_iso()                    << "\n";
        }

        // ── Crypto holdings ──
        for (auto& h : crypto_holds) {
            double cur_price = 0.0;
            for (auto& ct : crypto_ticks) {
                if (ct.symbol == h.symbol) { cur_price = ct.price; break; }
            }
            double cur_value  = cur_price * h.amount;
            double cost_basis = h.avg_cost * h.amount;
            double pnl        = cur_value - cost_basis;
            double pnl_pct    = cost_basis > 0 ? (pnl / cost_basis) * 100.0 : 0.0;

            grand_value += cur_value;
            grand_cost  += cost_basis;
            grand_pnl   += pnl;

            f << csv_str(h.symbol)              << ","
              << "CRYPTO"                       << ","
              << "Crypto"                       << ","
              << csv_dbl(h.amount, 8)           << ","
              << csv_dbl(h.avg_cost)            << ","
              << csv_dbl(cur_price)             << ","
              << csv_dbl(cur_value)             << ","
              << csv_dbl(cost_basis)            << ","
              << csv_dbl(pnl)                   << ","
              << csv_dbl(pnl_pct)               << ","
              << now_iso()                      << "\n";
        }

        // ── Summary row ──
        double grand_pct = grand_cost > 0 ? (grand_pnl / grand_cost) * 100.0 : 0.0;
        f << "\n";
        f << "TOTAL,,,,,,,"
          << csv_dbl(grand_value) << ","
          << csv_dbl(grand_cost)  << ","
          << csv_dbl(grand_pnl)   << ","
          << csv_dbl(grand_pct)   << ","
          << now_iso()            << "\n";

        f.close();
        return filename;
    }

    // Convenience: export only stock portfolio (no crypto)
    static std::string export_stocks_only(
        const std::vector<Stock>& stocks,
        const std::vector<Holding>& holds,
        const std::string& output_dir = ".")
    {
        return export_csv(stocks, holds, {}, {}, output_dir);
    }
};
