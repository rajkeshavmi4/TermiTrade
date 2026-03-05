# TermiTrade 
### A stock tracker that lives entirely in your terminal.

I built this because I wanted to track my portfolio without opening a browser tab every five minutes. Everything runs in the terminal: live prices, charts drawn in ASCII, a portfolio P&L tracker, and a price alert system. No browser, no Electron app, no nonsense.

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

## What it does

- Pulls live stock prices via the Alpha Vantage API (free tier works fine)
- Draws line charts directly in the terminal using ASCII and Unicode characters
- Lets you switch between 1D, 5D, 1M, 3M, and 1Y views
- Tracks your portfolio holdings and shows you live P&L per stock
- Fires alerts when a stock crosses a price you set
- Has a scrolling ticker tape at the top because it looks cool
- Works over SSH since there is zero GUI dependency

---

## Getting started

### What you need installed first
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev cmake build-essential

# macOS
brew install curl cmake
```

### Building it
```bash
git clone https://github.com/yourusername/termitrade
cd termitrade
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Running it

```bash
# Pass your API key directly (get a free one at alphavantage.co)
./termitrade YOUR_API_KEY

# Or export it as an environment variable
export AV_API_KEY=your_key_here
./termitrade

# No API key? Demo mode uses simulated data
./termitrade demo
```

---

## Keyboard shortcuts

| Key | What it does |
|-----|-------------|
| `↑` or `k` | Move up in the watchlist |
| `↓` or `j` | Move down in the watchlist |
| `1` | Switch to 1D chart |
| `5` | Switch to 5D chart |
| `m` | Switch to 1M chart |
| `3` | Switch to 3M chart |
| `y` | Switch to 1Y chart |
| `a` | Add a price alert for the selected stock |
| `p` | Add selected stock to your portfolio |
| `r` | Force refresh all prices |
| `q` | Quit |

---

## Configuration

Edit `config.json` before running to set up your own watchlist, portfolio, and alerts:

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

## Tech stack

| Part | What I used |
|------|-------------|
| Terminal UI | [FTXUI](https://github.com/ArthurSonzogni/FTXUI) |
| HTTP requests | libcurl |
| JSON parsing | [nlohmann/json](https://github.com/nlohmann/json) |
| Price data | [Alpha Vantage API](https://www.alphavantage.co/) |
| Build system | CMake with FetchContent |

---

## How the code is organized

```
termitrade/
├── src/
│   └── main.cpp          # entry point, layout, event loop
├── include/
│   ├── api_client.hpp    # Alpha Vantage HTTP wrapper
│   ├── chart.hpp         # ASCII chart renderer
│   ├── portfolio.hpp     # holdings and P&L logic
│   ├── watchlist.hpp     # stock data structures
│   └── alerts.hpp        # price alert engine
├── CMakeLists.txt
├── config.json.example
└── README.md
```

---

## What I want to add next

- [ ] Candlestick chart mode
- [ ] Indian market support (NSE/BSE)
- [ ] Crypto portfolio tracking
- [ ] Export portfolio summary to CSV
- [ ] Live reload config without restarting
- [ ] ncurses fallback for older terminals

---

## Contributing

This is a personal project I built to learn systems programming and TUI design. If you want to add something or fix a bug, open an issue and we can talk about it before you start coding.

---

## License

MIT. Use it however you want.

---

*Keshav Raj, DTU 2029*
