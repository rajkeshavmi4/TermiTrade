#pragma once
/*
 * ncurses_fallback.hpp — Minimal ncurses renderer for TermiTrade
 *
 * Activated automatically when:
 *   • The terminal reports TERM=dumb / TERM=vt100 / no color support
 *   • The --ncurses flag is passed on the command line
 *   • FTXUI fails to initialise (caught in main())
 *
 * Compile-time guard: only compiled when TERMITRADE_NCURSES=1 is defined
 * (set automatically by CMake when ncurses is found).
 *
 * Provides a self-contained blocking run loop that:
 *   - Draws a watchlist table
 *   - Shows portfolio P&L
 *   - Shows recent alerts
 *   - Refreshes on key-press or timer
 *   - Supports the same 'q' quit and arrow-key navigation
 */

#ifdef TERMITRADE_NCURSES
#include <ncurses.h>
#else
// Stub types so the rest of the project compiles without ncurses
typedef void WINDOW;
#endif

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <iomanip>

// Forward declarations — resolved from api_client.hpp / alerts.hpp
struct Stock;
struct Holding;
struct Alert;
enum class AlertType;

#ifdef TERMITRADE_NCURSES

// ──────────────────────────────────────────────
//  Color pair IDs
// ──────────────────────────────────────────────
enum NcColors {
    NC_GREEN   = 1,
    NC_RED     = 2,
    NC_YELLOW  = 3,
    NC_CYAN    = 4,
    NC_GRAY    = 5,
    NC_HEADER  = 6,   // black on yellow
    NC_SEL     = 7,   // black on white (selected row)
};

// ──────────────────────────────────────────────
//  Formatting helpers (duplicated here so
//  ncurses_fallback.hpp is self-contained)
// ──────────────────────────────────────────────
static std::string nc_fmt_price(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
}
static std::string nc_fmt_pct(double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v << "%";
    return ss.str();
}

// ──────────────────────────────────────────────
//  NcursesApp
// ──────────────────────────────────────────────
class NcursesApp {
    std::vector<Stock>&   stocks_;
    std::vector<Holding>& holdings_;
    std::vector<Alert>&   alerts_ref_;  // most-recent first
    std::mutex&           mtx_;
    std::atomic<bool>&    running_;
    int                   selected_ = 0;

public:
    NcursesApp(std::vector<Stock>& st,
               std::vector<Holding>& h,
               std::vector<Alert>& al,
               std::mutex& m,
               std::atomic<bool>& r)
        : stocks_(st), holdings_(h), alerts_ref_(al), mtx_(m), running_(r) {}

    // Initialise ncurses and enter the event loop
    void run() {
        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE);   // non-blocking getch
        curs_set(0);

        if (has_colors()) {
            start_color();
            init_pair(NC_GREEN,  COLOR_GREEN,  COLOR_BLACK);
            init_pair(NC_RED,    COLOR_RED,    COLOR_BLACK);
            init_pair(NC_YELLOW, COLOR_YELLOW, COLOR_BLACK);
            init_pair(NC_CYAN,   COLOR_CYAN,   COLOR_BLACK);
            init_pair(NC_GRAY,   COLOR_WHITE,  COLOR_BLACK);
            init_pair(NC_HEADER, COLOR_BLACK,  COLOR_YELLOW);
            init_pair(NC_SEL,    COLOR_BLACK,  COLOR_WHITE);
        }

        auto last_draw = std::chrono::steady_clock::now();

        while (running_) {
            auto now = std::chrono::steady_clock::now();
            // Redraw every second or on keypress
            bool timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - last_draw).count() >= 1000;

            int ch = getch();
            if (ch == 'q' || ch == 'Q') { running_ = false; break; }
            if (ch == KEY_DOWN || ch == 'j') {
                std::lock_guard<std::mutex> lk(mtx_);
                selected_ = std::min((int)stocks_.size()-1, selected_+1);
            }
            if (ch == KEY_UP || ch == 'k') {
                std::lock_guard<std::mutex> lk(mtx_);
                selected_ = std::max(0, selected_-1);
            }

            if (ch != ERR || timeout) {
                draw();
                last_draw = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        endwin();
    }

private:
    void draw() {
        std::lock_guard<std::mutex> lk(mtx_);
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        erase();

        // ── Title bar ──
        attron(COLOR_PAIR(NC_HEADER) | A_BOLD);
        std::string title = " TermiTrade  [ncurses mode]  Press Q to quit ";
        mvprintw(0, 0, "%-*s", cols, title.c_str());
        attroff(COLOR_PAIR(NC_HEADER) | A_BOLD);

        // ── Ticker tape ──
        int tx = 0;
        mvprintw(1, tx, " ");
        for (auto& s : stocks_) {
            bool up = s.change >= 0;
            if (has_colors()) attron(COLOR_PAIR(up ? NC_GREEN : NC_RED));
            std::string tick = s.symbol + " $" + nc_fmt_price(s.price)
                             + (up ? " +" : " ") + nc_fmt_pct(s.change_pct) + "  ";
            if (tx + (int)tick.size() < cols) {
                mvprintw(1, tx, "%s", tick.c_str());
                tx += tick.size();
            }
            if (has_colors()) attroff(COLOR_PAIR(up ? NC_GREEN : NC_RED));
        }

        // ── Separator ──
        mvhline(2, 0, ACS_HLINE, cols);

        // ── Watchlist header ──
        int y = 3;
        attron(COLOR_PAIR(NC_YELLOW) | A_BOLD);
        mvprintw(y++, 0, " %-6s %10s %9s %9s", "SYMBOL", "PRICE", "CHANGE", "CHG %");
        attroff(COLOR_PAIR(NC_YELLOW) | A_BOLD);
        mvhline(y++, 0, ACS_HLINE, 36);

        // ── Watchlist rows ──
        for (int i = 0; i < (int)stocks_.size() && y < rows - 6; i++) {
            auto& s = stocks_[i];
            bool up  = s.change >= 0;
            bool sel = (i == selected_);
            int color = up ? NC_GREEN : NC_RED;

            if (sel) attron(COLOR_PAIR(NC_SEL) | A_BOLD);
            else     attron(COLOR_PAIR(color));

            mvprintw(y, 0, " %-6s %10s %9s %8s%%",
                s.symbol.c_str(),
                nc_fmt_price(s.price).c_str(),
                (std::string(up?"+":"")+nc_fmt_price(s.change)).c_str(),
                nc_fmt_pct(std::abs(s.change_pct)).c_str());
            clrtoeol();

            if (sel) attroff(COLOR_PAIR(NC_SEL) | A_BOLD);
            else     attroff(COLOR_PAIR(color));
            y++;
        }

        // ── Portfolio panel (right side) ──
        int px = 40, py = 3;
        attron(COLOR_PAIR(NC_CYAN) | A_BOLD);
        mvprintw(py++, px, " %-6s %8s %10s", "SYMBOL", "VALUE", "P&L");
        attroff(COLOR_PAIR(NC_CYAN) | A_BOLD);
        mvhline(py++, px, ACS_HLINE, 26);

        double tot_val = 0, tot_pnl = 0;
        for (auto& h : holdings_) {
            double cur = 0;
            for (auto& s : stocks_) if (s.symbol == h.symbol) { cur = s.price; break; }
            double val = cur * h.shares;
            double pnl = val - h.avg_cost * h.shares;
            tot_val += val; tot_pnl += pnl;
            attron(COLOR_PAIR(pnl >= 0 ? NC_GREEN : NC_RED));
            mvprintw(py++, px, " %-6s %8s %+10s",
                h.symbol.c_str(),
                nc_fmt_price(val).c_str(),
                nc_fmt_price(pnl).c_str());
            attroff(COLOR_PAIR(pnl >= 0 ? NC_GREEN : NC_RED));
        }
        mvhline(py++, px, ACS_HLINE, 26);
        attron(A_BOLD | COLOR_PAIR(tot_pnl >= 0 ? NC_GREEN : NC_RED));
        mvprintw(py++, px, " %-6s %8s %+10s",
            "TOTAL",
            nc_fmt_price(tot_val).c_str(),
            nc_fmt_price(tot_pnl).c_str());
        attroff(A_BOLD | COLOR_PAIR(tot_pnl >= 0 ? NC_GREEN : NC_RED));

        // ── Alerts ──
        py += 2;
        attron(COLOR_PAIR(NC_YELLOW) | A_BOLD);
        mvprintw(py++, px, " RECENT ALERTS");
        attroff(COLOR_PAIR(NC_YELLOW) | A_BOLD);
        for (auto& a : alerts_ref_) {
            if (py >= rows - 2) break;
            int c = (a.type == AlertType::UP)   ? NC_GREEN  :
                    (a.type == AlertType::DOWN)  ? NC_RED    : NC_YELLOW;
            attron(COLOR_PAIR(c));
            mvprintw(py++, px, " %s  %s",
                a.timestamp.c_str(),
                a.message.substr(0, cols - px - 3).c_str());
            attroff(COLOR_PAIR(c));
        }

        // ── Status bar ──
        mvhline(rows-2, 0, ACS_HLINE, cols);
        attron(COLOR_PAIR(NC_GRAY));
        mvprintw(rows-1, 0,
            " j/k: navigate   q: quit   r: refresh  |  TermiTrade ncurses v1.0");
        attroff(COLOR_PAIR(NC_GRAY));

        refresh();
    }
};

#else  // !TERMITRADE_NCURSES

// ──────────────────────────────────────────────
//  Stub class when ncurses is not available
// ──────────────────────────────────────────────
class NcursesApp {
public:
    template<typename... Args>
    NcursesApp(Args&&...) {}

    void run() {
        // ncurses not compiled in — print a plain-text summary to stdout
        printf("ncurses support not compiled in.\n");
        printf("Rebuild with: cmake .. -DUSE_NCURSES=ON\n");
    }
};

#endif  // TERMITRADE_NCURSES
