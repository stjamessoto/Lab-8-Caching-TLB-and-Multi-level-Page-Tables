# CS375 Lab 8 – Caching, TLB and Multi-level Page Tables

A C++ simulator of virtual-to-physical address translation with a direct-mapped page table, a
2-level page table, a 16-entry LRU TLB and simulated page faults, plus a LaTeX report.

## ✅ What's left to do

1. Take the 11 screenshots in the [table below](#-screenshots) and save them in `report/screenshots/`.
2. Rebuild the report and the submission folder:
   ```bash
   cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables
   make submission
   ```
3. Open `submission/report.pdf` and check that no "Screenshot missing" boxes remain.
4. Drag the 5 files in `submission/` into Canvas, individually and not zipped.

## 📁 Folder layout

```
Lab-8-Caching-TLB-and-Multi-level-Page-Tables/
├── submission/                  ← ✅ DRAG THESE 5 FILES INTO CANVAS
│   ├── main.cpp                 Copy of src/main.cpp
│   ├── generate_addresses.cpp   Copy of src/generate_addresses.cpp
│   ├── Makefile                 Copy of src/Makefile
│   ├── addresses.txt            Copy of src/addresses.txt
│   └── report.pdf               Copy of report/report.pdf
├── src/                         Source code: edit files here, not in submission/
│   ├── main.cpp                 The simulator (Parts 1-4)
│   ├── generate_addresses.cpp   Writes addresses.txt
│   ├── Makefile                 The Makefile submitted to Canvas
│   ├── addresses.txt            1000-address input trace
│   └── sample_addresses.txt     The handout's sample addresses, for the Part 1 screenshot
├── report/
│   ├── report.tex               LaTeX source for the report
│   ├── report.pdf               The built report
│   ├── data/                    Numbers for the tables and graphs, written by make data
│   └── screenshots/             ← PUT YOUR 11 PNG SCREENSHOTS HERE
├── Makefile                     Shortcuts, listed below
├── .gitignore
└── README.md
```

## ✅ What to submit on Canvas

| File | Why |
|------|-----|
| `main.cpp` | The simulator |
| `Makefile` | Builds the lab |
| `addresses.txt` | The input trace |
| `report.pdf` | The analysis report, with screenshots |
| `generate_addresses.cpp` | Not on the handout's list, but the Makefile needs it to build |

All five are in `submission/`.

## 🔧 Setup (Ubuntu / WSL)

```bash
sudo apt update && sudo apt install build-essential
sudo apt install texlive-latex-extra texlive-pictures latexmk   # only needed to build the report
```

## ▶️ Build and run

```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
make
./lab
```

The grader can run the same `make` and `./lab` inside a folder holding just the submitted files.

| Command | What it runs |
|---------|--------------|
| `./lab` | Part 4: 2-level table, TLB, 256 frames and page faults |
| `./lab --part 1 --file sample_addresses.txt` | Part 1: direct-mapped table |
| `./lab --part 2` | Part 2: 2-level table |
| `./lab --part 3` | Part 3: adds the TLB |
| `./lab --no-tlb --quiet` | Part 4 without the TLB, summary only |
| `./lab --tlb 32` | Use a different TLB size |
| `./lab --runs 10` | Average the hit rate over 10 random seeds |
| `./lab --experiment` | Run the TLB size and working set experiments |
| `./lab --help` | List every option |

Runs use seed 42 by default, so the same command prints the same results. Only the measured
time changes from run to run.

## 🛠️ Makefile shortcuts

Run these from the repo root:

| Command | What it does |
|---------|--------------|
| `make` | Builds `lab` and `generate`, and creates `addresses.txt` |
| `make run` | Builds and runs Part 4 |
| `make data` | Reruns the experiments and updates `report/data/` |
| `make report` | Builds `report/report.pdf` and removes LaTeX's working files |
| `make submission` | Rebuilds the report, refreshes `submission/` and removes the compiled programs |
| `make clean` | Removes the compiled programs and `src/addresses.txt` (`make` recreates it) |

## 📸 Screenshots

Enlarge the terminal, press **Ctrl+L** to clear it, paste the commands, then capture from the
last command line down to the end of its output. On Windows, **Win+Shift+S** takes the capture.
Save each one in `report/screenshots/` with the exact file name below, and the report
places it in the right section automatically. Until then, the report shows a
"Screenshot missing" box.

| # | File name | Report section | Capture this |
|---|-----------|----------------|--------------|
| 1 | `01_build.png` | 3 Screenshots | The compile lines |
| 2 | `02_part1.png` | 3 Screenshots | The warning, 6 `VA → PA` lines and the summary |
| 3 | `03_part2.png` | 3 Screenshots | The last `VA → PA` line and the summary |
| 4 | `04_part3.png` | 3 Screenshots | 5 `[TLB HIT]` lines and the summary |
| 5 | `05_first_10.png` | 3 Screenshots | All 10 lines |
| 6 | `06_last_10.png` | 3 Screenshots | All 10 lines |
| 7 | `07_page_faults.png` | 3 Screenshots | 10 `PAGE FAULT` lines and the total, `544` |
| 8 | `08_hit_rate.png` | 3 Screenshots | The 10-run table and the `Average` line |
| 9 | `09_no_tlb.png` | 3 Screenshots | The summary showing `TLB: disabled` |
| 10 | `10_tlb_size_2.png` | 5 Experiment | The whole TLB size table |
| 11 | `11_working_set.png` | 4 Performance Analysis | Both the working set and time tables |

### Commands for each screenshot

Run `01_build.png` first, because it compiles the `lab` program the others use.

**01_build.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
make clean && make
```

**02_part1.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab --part 1 --file sample_addresses.txt
```

**03_part2.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab --part 2 | tail -n 9
```

**04_part3.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab --part 3 | grep "TLB HIT" | head -n 5
./lab --part 3 --quiet
```

**05_first_10.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab | head -n 10
```

**06_last_10.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab | tail -n 10
```

**07_page_faults.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab | grep "evicted" | head -n 10
./lab | grep -c "PAGE FAULT"
```

**08_hit_rate.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab --runs 10
```

**09_no_tlb.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab --no-tlb --quiet
```

**10_tlb_size_2.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab --experiment | head -n 8
```

**11_working_set.png**
```bash
cd ~/Lab-8-Caching-TLB-and-Multi-level-Page-Tables/src
./lab --experiment | tail -n +10
```

## 📊 Results

Default run on `addresses.txt`: 4.30% TLB hit rate, 544 page faults, 192.40 modeled cycles per
translation, compared with 200 without a TLB.

Two results in the report can look wrong at first, and the report explains both:

- **The hit rate on `addresses.txt` is low.** The trace has almost no locality. The report also
  runs a locality trace, which reaches about 89%.
- **The measured time is slower with the TLB.** The time measures this C++ program, where a loop
  over 16 entries costs more than two array reads. The modeled cycles show the hardware comparison.

## 🐞 Changes from the handout's skeleton

| Problem | Fix |
|---------|-----|
| `allocate_frame` hangs once all 256 frames are used | Added random eviction |
| Evicted pages would stay in the TLB | Added `TLB::invalidate` |
| The 48-bit `0x7fff5fbff000` silently ends input reading | Addresses are read as 64 bits and reduced to 32 with a warning |
| Summary counts print in hexadecimal | Hex formatting is confined to a helper |
| `make run` runs `./generate` without building it | `addresses.txt` is a Makefile target |
| The timer includes terminal printing | Time is measured with printing off |
| The generator's comment says the hot region is 100 pages | Corrected to 256 pages; the code is unchanged |
