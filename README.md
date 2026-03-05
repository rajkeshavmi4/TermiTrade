# TermiTrade 📈
### Bloomberg Terminal vibes. Open source. In your terminal.

A real-time stock portfolio tracker with a beautiful TUI (Terminal UI) built in **C++** using FTXUI. Live prices, ASCII charts, portfolio P&L tracking, and a price alert system — all without leaving your terminal.

```
╔══════════════════════════════════════════════════════════════════════════════╗
║ ◆ LIVE │ AAPL $182.30 ▲1.19% │ NVDA $621.40 ▲3.04% │ TSLA $248.20 ▼1.51% ║
╠══════════╦═══════════════════════════════════════════╦════════════════════════╣
║ WATCHLIST║  AAPL  $182.30  ▲+2.14 (+1.19%)          ║ PORTFOLIO              ║
║──────────║  O:180.16 H:183.10 L:179.88 VOL:54.2M    ║ AAPL  $1823   +$223   ║
║ AAPL ▲   ║  [1D] 5D  1M  3M  1Y                     ║ NVDA  $3107   +$157   ║
║ GOOGL ▼  ║                                           ║ MSFT  $3028   +$228   ║
║ MSFT  ▲  ║   ┤183.10                                 ║ TSLA  $3723   +$423   ║
║ TSLA  ▼  ║   │          ╭──╮                         ║────────────────────────║
║ NVDA  ▲  ║   │    ╭─╮  ╭╯  ╰──╮    ╭╮               ║ TOTAL $11,681  +$1031  ║
║ META  ▲  ║   │╭──╯ ╰──╯       ╰────╯╰─╮             ╠════════════════════════╣
║ AMZN  ▼  ║   │                         ╰──           ║ ALERTS                 ║
║ NFLX  ▲  ║   ┤179.88                                 ║ 14:23 AAPL above $190 ║
║          ║   └───────────────────────────            ║ 14:18 NVDA RSI>70     ║
╚══════════╩═══════════════════════════════════════════╩════════════════════════╝
║ ● NIFTY: 21,843 ▲  │  SENSEX: 72,186 ▲  │  BTC: $67,420 ▲  │  14:31:22 IST ║
```

---

## Features

- **Real-time prices** via Alpha Vantage API (free tier: 25 req/day)
- **ASCII line charts** with Unicode fill — rendered entirely in terminal
- **Multi-timeframe** — 1D / 5D / 1M / 3M / 1Y
- **Portfolio tracker** — P&L per holding + total portfolio value live
- **Price alert system** — set threshold alerts, get notified in-app
- **Scrolling ticker tape** across the top
- **Keyboard navigation** — vim-style (j/k) or arrow keys
- **Zero dependencies on GUI** — pure terminal, works over SSH

---

## Installation

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev cmake build-essential

# macOS
brew install curl cmake
```

### Build
```bash
git clone https://github.com/yourusername/termitrade
cd termitrade
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Run
```bash
# With Alpha Vantage API key (free at alphavantage.co)
./termitrade YOUR_API_KEY

# Or set env variable
export AV_API_KEY=your_key_here
./termitrade

# Demo mode (no API key needed, uses simulated data)
./termitrade demo
```

---

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `↑` / `k` | Move up in watchlist |
| `↓` / `j` | Move down in watchlist |
| `1` | Switch to 1D chart |
| `5` | Switch to 5D chart |
| `m` | Switch to 1M chart |
| `3` | Switch to 3M chart |
| `y` | Switch to 1Y chart |
| `a` | Add price alert for selected stock |
| `p` | Add stock to portfolio |
| `r` | Force refresh prices |
| `q` | Quit |

---

## Configuration

Edit `config.json` to set your watchlist, portfolio, and alerts:

```json
{
  "api_key": "your_key_here",
  "watchlist": ["AAPL", "GOOGL", "MSFT", "TSLA", "NVDA"],
  "portfolio": [
    { "symbol": "AAPL", "shares": 10, "avg_cost": 160.00 },
    { "symbol": "NVDA", "shares": 5,  "avg_cost": 580.00 }
  ],
  "alerts": [
    { "symbol": "AAPL", "price": 190.0, "direction": "above" },
    { "symbol": "TSLA", "price": 240.0, "direction": "below" }
  ],
  "refresh_interval_seconds": 15
}
```

---

## Tech Stack

| Component | Technology |
|-----------|-----------|
| UI Framework | [FTXUI](https://github.com/ArthurSonzogni/FTXUI) — C++ TUI library |
| HTTP Client | libcurl |
| JSON Parser | [nlohmann/json](https://github.com/nlohmann/json) |
| Data Source | [Alpha Vantage API](https://www.alphavantage.co/) (free) |
| Build System | CMake + FetchContent |

---

## Project Structure

```
termitrade/
├── src/
│   └── main.cpp          # Entry point, layout, event loop
├── include/
│   ├── api_client.hpp    # Alpha Vantage HTTP wrapper
│   ├── chart.hpp         # ASCII/Unicode chart renderer
│   ├── portfolio.hpp     # Holdings & P&L calculation
│   ├── watchlist.hpp     # Stock data structures
│   └── alerts.hpp        # Price alert engine
├── CMakeLists.txt
├── config.json.example
└── README.md
```

---

## Roadmap

- [ ] Candlestick chart mode
- [ ] Indian market support (NSE/BSE via unofficial API)
- [ ] Crypto portfolio (Binance API)
- [ ] Export portfolio report to CSV
- [ ] Config file live reload
- [ ] `ncurses` fallback for older terminals

---

## Contributing

PRs welcome! This is a learning project — if you're a fellow student at DTU or anywhere else and want to contribute, open an issue first.

---

## License

MIT — use it, fork it, build on it.

---

*Built by Keshav Raj — DTU'29*
