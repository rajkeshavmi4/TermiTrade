# TermiTrade v1.1
### A stock tracker that lives entirely in your terminal.

Built because watching a portfolio shouldn't require opening a browser. Everything runs in the terminal: live prices, line **and candlestick** charts drawn in Unicode, a combined stock+crypto P&L tracker, price alerts, CSV export, live config reload, and a ncurses fallback for SSH over slow connections.

```
╔══════════════════════════════════════════════════════════════════════════════════╗
║ ◆ LIVE │ AAPL $182.30 ▲1.19% │ TCS $3820 ▲0.8% │ ₿BTC $67,420 │ NVDA ▲3.04% ║
║ [ STOCKS ]   CRYPTO     PORTFOLIO           Tab: switch panel                   ║
╠══════════════╦═══════════════════════════════════════╦══════════════════════════╣
║  SYM  MKT  $ ║ AAPL [US] $182.30 ▲+2.14 (1.19%)     ║ PORTFOLIO                ║
║──────────────║ O:180 H:183 L:179 VOL:54.2M            ║ AAPL  $1823  +$223 +14% ║
║ AAPL  US  ▲ ▁▃▇║ CANDLE  [1D] 5D 1M 3M 1Y              ║ TCS   $3820  +$420 +12% ║
║ GOOGL US  ▼ ▇▄▂║                                       ║ NVDA  $3107  +$157  +5% ║
║ TCS   NSE ▲ ▂▅▇║   ┃ ┃    ┃┃ ┃                        ║ TSLA  $3723  +$423 +19% ║
║ RELI  BSE ▼ ▅▃▁║  ╱┃╲┃   ╱┃╲┃╲┃                       ║─── CRYPTO ───────────── ║
║ NVDA  US  ▲ ▁▄▇║ ─┘  └───┘  └─┘                       ║ BTC   $3360  +$360 +12% ║
║ META  US  ▲ ▃▅▇║                                       ║──────────────────────── ║
║           ║                                       ║ TOTAL $15,033 +$1583       ║
╠══════════════╬═══════════════════════════════════════╬══════════════════════════╣
║              ║                                       ║ ALERTS                   ║
║              ║                                       ║ 14:23 AAPL above $190    ║
║              ║                                       ║ 14:18 TCS crossed $3800  ║
╚══════════════╩═══════════════════════════════════════╩══════════════════════════╝
║ ● FEED ACTIVE │ ✔ Saved: termitrade_portfolio_20250313_143021.csv │ 14:31:22 IST ║
```

---

## What's new in v1.1

| Feature | Details |
|---------|---------|
| **Candlestick chart** | Press `c` to toggle between line and candlestick mode. OHLCV data is fetched separately from the Alpha Vantage `TIME_SERIES_INTRADAY` / `TIME_SERIES_DAILY` endpoints and rendered with braille-pixel Unicode box bodies and wick lines. |
| **Indian market support** | Add stocks with `"market": "NSE"` or `"market": "BSE"` in `config.json`. The app appends `.NSE` / `.BSE` to the symbol string, which Alpha Vantage's Global Quote endpoint accepts natively (e.g. `TCS.NSE`, `RELIANCE.BSE`). A market badge next to each symbol shows which exchange it's from. |
| **Crypto portfolio** | Add `"market": "CRYPTO"` entries to your watchlist. Live prices are fetched via the `CURRENCY_EXCHANGE_RATE` endpoint, historical daily closes via `DIGITAL_CURRENCY_DAILY`. Crypto holdings appear in a separate section of the portfolio panel with fractional unit support. Press `Tab` to reach the dedicated Crypto tab. |
| **CSV export** | Press `e` at any time. A `termitrade_portfolio_YYYYMMDD_HHMMSS.csv` file is written to the current directory with columns: Symbol, Market, Type, Units, AvgCostUSD, CurrentPriceUSD, CurrentValueUSD, CostBasisUSD, PnLUSD, PnLPct, LastUpdated. A TOTAL summary row is appended. |
| **Live config reload** | Edit `config.json` while TermiTrade is running — the background thread checks the file's modification time every 15 seconds and re-applies any changes to the watchlist, portfolio, and alerts without restarting. A `⟳ Config reloaded` message appears in the status bar. |
| **ncurses fallback** | Pass `--ncurses` or set `TERM=dumb` to activate a minimal ncurses renderer. Useful over slow SSH, in tmux without 24-bit colour, or on legacy terminals. Compiled conditionally — requires `libncurses-dev`; CMake detects it automatically. |

---

## Getting started

### Prerequisites
```bash
# Ubuntu / Debian
sudo apt-get install libcurl4-openssl-dev cmake build-essential libncurses-dev

# macOS
brew install curl cmake ncurses
```

### Build
```bash
git clone https://github.com/yourusername/termitrade
cd termitrade
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

To build **without** ncurses (e.g. on a system where it's unavailable):
```bash
cmake .. -DUSE_NCURSES=OFF
```

### Run
```bash
# Pass API key as argument (free key at alphavantage.co)
./termitrade YOUR_API_KEY

# Or export to environment
export AV_API_KEY=your_key
./termitrade

# Demo mode (sine-wave simulated data, no API key needed)
./termitrade demo

# Force ncurses fallback
./termitrade --ncurses

# Custom config file
./termitrade --config ~/.config/termitrade.json YOUR_API_KEY
```

---

## Configuration (`config.json`)

The file is created automatically on first run. Edit it while the app is running — changes are picked up within 15 seconds.

```json
{
  "api_key": "your_key_here",
  "refresh_interval_seconds": 15,
  "default_timeframe": "1D",

  "watchlist": [
    { "symbol": "AAPL",      "market": "US"     },
    { "symbol": "TCS",       "market": "NSE"    },
    { "symbol": "RELIANCE",  "market": "BSE"    },
    { "symbol": "BTC",       "market": "CRYPTO" },
    { "symbol": "ETH",       "market": "CRYPTO" }
  ],

  "portfolio": [
    { "symbol": "AAPL",     "shares": 10,   "avg_cost": 160.00, "asset_type": "stock"  },
    { "symbol": "TCS",      "shares": 5,    "avg_cost": 3400.0, "asset_type": "stock"  },
    { "symbol": "BTC",      "shares": 0.05, "avg_cost": 42000.0,"asset_type": "crypto" },
    { "symbol": "ETH",      "shares": 0.5,  "avg_cost": 2800.0, "asset_type": "crypto" }
  ],

  "alerts": [
    { "symbol": "AAPL",     "price": 190.0,  "direction": "above" },
    { "symbol": "TCS",      "price": 3800.0, "direction": "above" },
    { "symbol": "BTC",      "price": 60000,  "direction": "below" }
  ]
}
```

### Supported markets

| `"market"` value | Exchange | Example symbols |
|---|---|---|
| `"US"` | NASDAQ / NYSE | `AAPL`, `NVDA`, `TSLA` |
| `"NSE"` | National Stock Exchange (India) | `TCS`, `INFY`, `HDFCBANK` |
| `"BSE"` | Bombay Stock Exchange (India) | `RELIANCE`, `WIPRO` |
| `"CRYPTO"` | Crypto via Alpha Vantage | `BTC`, `ETH`, `SOL`, `DOGE` |

---

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `↑` / `k` | Move up in watchlist |
| `↓` / `j` | Move down in watchlist |
| `Tab` | Cycle panels: Stocks → Crypto → Portfolio |
| `c` | Toggle chart mode: Line ↔ Candlestick |
| `1` | 1-day chart (5-min candles) |
| `5` | 5-day chart |
| `m` | 1-month chart |
| `3` | 3-month chart |
| `y` | 1-year chart |
| `r` | Force-refresh prices |
| `e` | Export portfolio to CSV |
| `q` | Quit |

---

## How it works

### Candlestick rendering

OHLCV data is fetched from `TIME_SERIES_INTRADAY` (for 1D) or `TIME_SERIES_DAILY` (all other timeframes) — the same endpoints used for line data, but with all four price fields parsed. Each candle is drawn on an FTXUI braille-pixel `Canvas`:

- **Wick**: a thin `DrawPointLine` from `low` to `high`
- **Body**: a filled block of `DrawPoint` calls between `open` and `close`
- Green body = close ≥ open; Red body = close < open

### Indian market support

Alpha Vantage's `GLOBAL_QUOTE` function accepts symbol strings with exchange suffixes out of the box. TermiTrade appends `.NSE` or `.BSE` when sending API requests for those market types, and strips the suffix for display.

### Crypto prices

Two endpoints are used:
- `CURRENCY_EXCHANGE_RATE` — live spot price (refreshed every 15 s with other quotes)
- `DIGITAL_CURRENCY_DAILY` — daily close history for the sparkline and chart

Crypto holdings track fractional amounts (e.g. 0.05 BTC) and appear in a dedicated section of the portfolio panel, contributing to the overall P&L total.

### Live config reload

`ConfigLoader` stores the file's `mtime` on each load. The background price-updater thread calls `reload_if_changed()` once per update cycle. If the mtime has advanced, the config is re-parsed and `apply_config()` merges in new symbols while preserving live prices for symbols that didn't change.

### ncurses fallback

Compiled in when `libncurses-dev` is present and `USE_NCURSES=ON` (the default). At runtime it's activated by `--ncurses` or when `TERM` is `dumb`/`unknown`. The `NcursesApp` class provides a blocking loop drawing a watchlist table, portfolio, and alerts using `ncurses` primitives, with identical price-update threading to the main FTXUI path.

---

## Tech stack

| Component | Library |
|-----------|---------|
| Terminal UI | [FTXUI v5](https://github.com/ArthurSonzogni/FTXUI) |
| ncurses fallback | libncurses |
| HTTP | libcurl |
| JSON | [nlohmann/json v3.11](https://github.com/nlohmann/json) |
| Price data | [Alpha Vantage API](https://www.alphavantage.co/) |
| Build | CMake 3.14+ with FetchContent |

---

## Code structure

```
termitrade/
├── src/
│   └── main.cpp              # entry point, layout, event loop, panel routing
├── include/
│   ├── api_client.hpp        # Alpha Vantage HTTP wrapper (stocks + crypto + Indian)
│   ├── chart.hpp             # line chart + candlestick renderer, CandleData struct
│   ├── crypto.hpp            # CryptoTick, CryptoPortfolio, AssetType helpers
│   ├── config.hpp            # ConfigLoader — live-reload from config.json
│   ├── export.hpp            # PortfolioExporter — CSV writer
│   └── ncurses_fallback.hpp  # NcursesApp — minimal ncurses renderer
├── CMakeLists.txt            # build system; optional USE_NCURSES flag
├── config.json.example       # template config
└── README.md
```

---

## Roadmap

- [ ] WebSocket streaming (Polygon.io or Finnhub) to replace poll-based updates
- [ ] Candlestick pattern recognition labels (doji, hammer, engulfing)
- [ ] RSI / MACD overlay on chart
- [ ] Sector heatmap panel
- [ ] Paper-trading mode (simulate buys/sells against live prices)
- [ ] Export to PDF report

---

## Contributing

Personal project — open an issue before submitting a PR so we can align on scope.

---

## License

MIT.

---

*Keshav Raj, DTU 2029*
