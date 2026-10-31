// CS375 Lab 8: Caching, TLB and Multi-level Page Tables
//
// Simulates virtual-to-physical address translation for 32-bit addresses
// with 4 KB pages:
//   Part 1  direct-mapped page table (1M entries)
//   Part 2  2-level page table (10-bit L1, 10-bit L2, 12-bit offset)
//   Part 3  16-entry fully associative TLB with LRU replacement
//   Part 4  256 physical frames, random invalidation and random eviction
//
// Run ./lab --help for the options.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace std::chrono;

// === Config ===
const int PAGE_SIZE = 4096;
const int PAGE_OFFSET_BITS = 12;
const int VPN_BITS = 20;
const int L1_BITS = 10, L2_BITS = 10;
const int PD_ENTRIES = 1 << L1_BITS;
const int PT_ENTRIES = 1 << L2_BITS;
const uint32_t NUM_VPNS = 1u << VPN_BITS;

const uint32_t NUM_FRAMES = 256;        // Part 4 physical memory limit
const uint32_t UNLIMITED_FRAMES = NUM_VPNS;  // Parts 1-3: one frame per page, so nothing is evicted
const int DEFAULT_TLB_ENTRIES = 16;

// Modeled hardware costs. The simulator's own speed says little about real
// hardware, so the report compares translations using these figures.
const uint64_t TLB_LOOKUP_CYCLES = 1;
const uint64_t MEMORY_ACCESS_CYCLES = 100;

// === Page Table Structures ===
struct PageTableEntry {
    bool valid = false;
    uint32_t frame = 0;
};

struct PageDirectoryEntry {
    bool present = false;
    PageTableEntry* pt = nullptr;
    PageDirectoryEntry() : pt(nullptr) {}
    ~PageDirectoryEntry() { if (pt) delete[] pt; }
};

PageDirectoryEntry page_directory[PD_ENTRIES];
vector<PageTableEntry> flat_page_table;  // Part 1 only

// === Simulation Settings and Counters ===
struct SimConfig {
    bool flat_table = false;      // Part 1: use the direct-mapped table
    bool use_tlb = true;
    int tlb_entries = DEFAULT_TLB_ENTRIES;
    uint32_t frames = NUM_FRAMES;
    double invalidate_rate = 0.01;  // chance per access of invalidating a random page
    uint32_t seed = 42;
    bool print_lines = false;     // print each VA -> PA translation
    bool print_faults = false;    // print PAGE FAULT and INVALIDATE messages
};

struct RunStats {
    uint64_t translations = 0;
    uint64_t tlb_hits = 0;
    uint64_t tlb_misses = 0;
    uint64_t page_faults = 0;
    uint64_t evictions = 0;
    uint64_t invalidations = 0;
    uint64_t page_tables = 0;     // second-level tables allocated
    uint64_t cycles = 0;          // modeled translation cycles

    double hit_rate() const {
        uint64_t lookups = tlb_hits + tlb_misses;
        return lookups ? tlb_hits * 100.0 / lookups : 0.0;
    }
    double avg_cycles() const {
        return translations ? static_cast<double>(cycles) / translations : 0.0;
    }
};

SimConfig config;
RunStats stats;
mt19937 rng(42);

// === Physical Memory Simulation ===
vector<uint32_t> free_frames;       // popped from the back
vector<uint32_t> allocated_frames;  // frames holding a page, in no particular order
vector<uint32_t> frame_owner;       // frame -> VPN of the page stored in it

// === TLB ===
struct TLBEntry {
    uint32_t vpn = 0, pfn = 0;
    uint64_t timestamp = 0;
    bool valid = false;
};

// Fully associative with LRU replacement. Every access stamps the entry with
// an increasing clock value, so the smallest timestamp is least recently used.
// An empty slot has timestamp 0 and is therefore always chosen first. The
// size is a constructor argument so the report can test 4, 8, 16 and 32.
class TLB {
    vector<TLBEntry> entries;
    uint64_t clock = 0;
    uint64_t hits = 0, misses = 0;

public:
    explicit TLB(int size = DEFAULT_TLB_ENTRIES) : entries(size) {}

    bool lookup(uint32_t vpn, uint32_t& pfn) {
        for (auto& e : entries) {
            if (e.valid && e.vpn == vpn) {
                e.timestamp = ++clock;
                pfn = e.pfn;
                hits++;
                return true;
            }
        }
        misses++;
        return false;
    }

    void insert(uint32_t vpn, uint32_t pfn) {
        size_t lru_idx = 0;
        uint64_t oldest = entries[0].timestamp;
        for (size_t i = 1; i < entries.size(); i++) {
            if (entries[i].timestamp < oldest) {
                oldest = entries[i].timestamp;
                lru_idx = i;
            }
        }
        entries[lru_idx].vpn = vpn;
        entries[lru_idx].pfn = pfn;
        entries[lru_idx].timestamp = ++clock;
        entries[lru_idx].valid = true;
    }

    // Called when a page loses its frame. Without this the TLB would keep
    // translating the page to a frame that now belongs to another page.
    void invalidate(uint32_t vpn) {
        for (auto& e : entries) {
            if (e.valid && e.vpn == vpn) {
                e.valid = false;
                e.timestamp = 0;
            }
        }
    }

    bool contains(uint32_t vpn) const {
        for (const auto& e : entries) {
            if (e.valid && e.vpn == vpn) return true;
        }
        return false;
    }

    uint64_t hit_count() const { return hits; }
    uint64_t miss_count() const { return misses; }
};

TLB tlb;

// === Helper Functions ===
uint32_t get_l1_index(uint32_t va) { return (va >> (PAGE_OFFSET_BITS + L2_BITS)) & (PD_ENTRIES - 1); }
uint32_t get_l2_index(uint32_t va) { return (va >> PAGE_OFFSET_BITS) & (PT_ENTRIES - 1); }
uint32_t get_offset(uint32_t va) { return va & (PAGE_SIZE - 1); }
uint32_t get_vpn(uint32_t va) { return va >> PAGE_OFFSET_BITS; }
uint32_t make_pa(uint32_t pfn, uint32_t va) { return (pfn << PAGE_OFFSET_BITS) | get_offset(va); }

string hex32(uint32_t value) {
    ostringstream out;
    out << "0x" << hex << setw(8) << setfill('0') << value;
    return out.str();
}

// Returns to the initial state, so one process can run many experiments.
void reset_simulation(const SimConfig& cfg) {
    config = cfg;
    stats = RunStats();
    rng.seed(cfg.seed);
    tlb = TLB(cfg.tlb_entries);

    for (auto& pde : page_directory) {
        delete[] pde.pt;
        pde.pt = nullptr;
        pde.present = false;
    }
    if (cfg.flat_table) flat_page_table.assign(NUM_VPNS, PageTableEntry());

    // Frames are handed out from the back, so unlimited memory gets frames
    // 0, 1, 2, ... in order. Limited memory shuffles them for random placement.
    free_frames.resize(cfg.frames);
    iota(free_frames.rbegin(), free_frames.rend(), 0u);
    if (cfg.frames < UNLIMITED_FRAMES) shuffle(free_frames.begin(), free_frames.end(), rng);
    allocated_frames.clear();
    frame_owner.assign(cfg.frames, 0);
}

size_t random_allocated_slot() {
    return uniform_int_distribution<size_t>(0, allocated_frames.size() - 1)(rng);
}

// Removes the page held by a frame from the page table and the TLB.
void unmap_frame(uint32_t frame) {
    uint32_t vpn = frame_owner[frame];
    page_directory[vpn >> L2_BITS].pt[vpn & (PT_ENTRIES - 1)].valid = false;
    tlb.invalidate(vpn);
}

// === Allocate Physical Frame ===
// Uses a free frame if there is one. Otherwise picks a random victim, so
// allocation can never run out of frames. Sets evicted when it took a victim.
uint32_t allocate_frame(uint32_t vpn, bool& evicted) {
    uint32_t frame;
    evicted = free_frames.empty();
    if (evicted) {
        frame = allocated_frames[random_allocated_slot()];
        unmap_frame(frame);
        stats.evictions++;
    } else {
        frame = free_frames.back();
        free_frames.pop_back();
        allocated_frames.push_back(frame);
    }
    frame_owner[frame] = vpn;
    return frame;
}

// Part 4: simulates the OS taking a page away, so its next access will fault.
void invalidate_random_page() {
    if (allocated_frames.empty()) return;
    size_t slot = random_allocated_slot();
    uint32_t frame = allocated_frames[slot];
    uint32_t vpn = frame_owner[frame];

    unmap_frame(frame);
    allocated_frames[slot] = allocated_frames.back();
    allocated_frames.pop_back();
    free_frames.push_back(frame);
    stats.invalidations++;

    if (config.print_faults) {
        cout << "INVALIDATE: VPN 0x" << hex << vpn << dec << " → freed frame " << frame << "\n";
    }
}

void handle_page_fault(uint32_t va, PageTableEntry& pte) {
    bool evicted;
    pte.frame = allocate_frame(get_vpn(va), evicted);
    pte.valid = true;
    stats.page_faults++;

    if (config.print_faults) {
        cout << "PAGE FAULT: VA " << hex32(va) << " → "
             << (evicted ? "evicted frame " : "free frame ") << pte.frame << "\n";
    }
}

// === Part 1: Direct-Mapped Page Table ===
uint32_t translate_direct(uint32_t va) {
    PageTableEntry& pte = flat_page_table[get_vpn(va)];
    stats.cycles += MEMORY_ACCESS_CYCLES;  // one memory reference
    if (!pte.valid) handle_page_fault(va, pte);
    return make_pa(pte.frame, va);
}

// === Part 2: 2-Level Page Table ===
uint32_t translate_2level(uint32_t va) {
    PageDirectoryEntry& pde = page_directory[get_l1_index(va)];
    stats.cycles += 2 * MEMORY_ACCESS_CYCLES;  // directory entry, then table entry

    // Second-level tables only exist for regions that are used. This is
    // where the 2-level design saves memory over the flat table.
    if (!pde.present) {
        pde.pt = new PageTableEntry[PT_ENTRIES];
        pde.present = true;
        stats.page_tables++;
    }

    PageTableEntry& pte = pde.pt[get_l2_index(va)];
    if (!pte.valid) handle_page_fault(va, pte);
    return make_pa(pte.frame, va);
}

// === Parts 3-4: Translation with TLB + 2-Level + Faults ===
// Returns true on a TLB hit. Sets fault if the access caused a page fault.
bool translate(uint32_t va, uint32_t& pa, bool& fault) {
    uint64_t faults_before = stats.page_faults;
    stats.translations++;
    fault = false;

    if (config.flat_table) {
        pa = translate_direct(va);
    } else if (!config.use_tlb) {
        pa = translate_2level(va);
    } else {
        // 1. TLB lookup
        uint32_t vpn = get_vpn(va), pfn;
        stats.cycles += TLB_LOOKUP_CYCLES;
        if (tlb.lookup(vpn, pfn)) {
            pa = make_pa(pfn, va);
            return true;
        }
        // 2. Page table walk, 3. update TLB
        pa = translate_2level(va);
        tlb.insert(vpn, pa >> PAGE_OFFSET_BITS);
    }

    fault = stats.page_faults != faults_before;
    return false;
}

// === Running a Trace ===
void maybe_invalidate() {
    if (config.invalidate_rate > 0.0 &&
        uniform_real_distribution<double>(0.0, 1.0)(rng) < config.invalidate_rate) {
        invalidate_random_page();
    }
}

void print_translation(uint32_t va, uint32_t pa, bool hit) {
    cout << "VA: " << hex32(va) << " → PA: " << hex32(pa);
    if (config.flat_table || !config.use_tlb) {
        cout << " (page " << showbase << hex << get_vpn(va) << noshowbase << dec << ")\n";
    } else {
        cout << (hit ? " [TLB HIT]\n" : " [TLB MISS]\n");
    }
}

RunStats run_trace(const vector<uint32_t>& trace, const SimConfig& cfg) {
    reset_simulation(cfg);
    for (uint32_t va : trace) {
        uint32_t pa;
        bool fault;
        maybe_invalidate();
        bool hit = translate(va, pa, fault);
        if (cfg.print_lines) print_translation(va, pa, hit);
    }
    stats.tlb_hits = tlb.hit_count();
    stats.tlb_misses = tlb.miss_count();
    return stats;
}

// Wall-clock microseconds per address, with printing switched off. Printing
// is thousands of times slower than translating and would hide the result.
// Each timed pass follows an untimed one, so the time is translation work and
// not the cost of allocating page tables from the heap.
double measure_avg_us(const vector<uint32_t>& trace, SimConfig cfg, int passes = 200) {
    cfg.print_lines = cfg.print_faults = false;
    nanoseconds total(0);
    volatile uint32_t sink = 0;
    auto replay = [&]() {
        for (uint32_t va : trace) {
            uint32_t pa;
            bool fault;
            maybe_invalidate();
            translate(va, pa, fault);
            sink = pa;
        }
    };
    for (int pass = 0; pass < passes; pass++) {
        reset_simulation(cfg);
        replay();
        auto start = high_resolution_clock::now();
        replay();
        total += duration_cast<nanoseconds>(high_resolution_clock::now() - start);
    }
    (void)sink;
    return total.count() / 1000.0 / (passes * static_cast<double>(trace.size()));
}

// === Trace Input and Synthetic Traces ===
// Reads each address as 64 bits first: reading "0x7fff5fbff000" straight into
// a uint32_t would fail and silently end the input. Wider addresses are
// reduced to their low 32 bits, because this is a 32-bit address space.
bool load_addresses(const string& path, vector<uint32_t>& addresses) {
    ifstream fin(path);
    if (!fin) return false;

    string token;
    while (fin >> token) {
        char* end = nullptr;
        unsigned long long value = strtoull(token.c_str(), &end, 16);
        if (end == token.c_str() || *end != '\0') {
            cerr << "WARNING: skipping invalid address '" << token << "'\n";
            continue;
        }
        if (value > 0xFFFFFFFFULL) {
            cout << "WARNING: " << token << " is wider than 32 bits, using "
                 << hex32(static_cast<uint32_t>(value)) << "\n";
        }
        addresses.push_back(static_cast<uint32_t>(value));
    }
    return true;
}

// Uniform accesses to exactly `pages` pages, for the working set experiment.
vector<uint32_t> make_working_set_trace(uint32_t pages, size_t length, uint32_t seed) {
    const uint32_t BASE = 0x10000000;
    mt19937 gen(seed);
    uniform_int_distribution<uint32_t> page(0, pages - 1);
    uniform_int_distribution<uint32_t> offset(0, PAGE_SIZE - 1);

    vector<uint32_t> trace(length);
    for (auto& va : trace) va = BASE + (page(gen) << PAGE_OFFSET_BITS) + offset(gen);
    return trace;
}

// Program-like locality: 90% of accesses step through a 64-page array in
// 64-byte strides, and 10% jump to a random page of a 256-page region.
vector<uint32_t> make_locality_trace(size_t length, uint32_t seed) {
    const uint32_t ARRAY_BASE = 0x20000000, ARRAY_BYTES = 64 * PAGE_SIZE, STRIDE = 64;
    const uint32_t HEAP_BASE = 0x30000000, HEAP_BYTES = 256 * PAGE_SIZE;
    mt19937 gen(seed);
    uniform_real_distribution<double> chance(0.0, 1.0);
    uniform_int_distribution<uint32_t> heap(0, HEAP_BYTES - 1);

    vector<uint32_t> trace(length);
    uint32_t position = 0;
    for (auto& va : trace) {
        if (chance(gen) < 0.9) {
            va = ARRAY_BASE + position;
            position = (position + STRIDE) % ARRAY_BYTES;
        } else {
            va = HEAP_BASE + heap(gen);
        }
    }
    return trace;
}

// === Reporting ===
void print_summary(const vector<uint32_t>& trace, const RunStats& s, const SimConfig& cfg) {
    cout << "\n=== SUMMARY ===\n"
         << "Total Addresses: " << s.translations << "\n"
         << "Page Faults: " << s.page_faults << " (evictions: " << s.evictions
         << ", invalidations: " << s.invalidations << ")\n";

    if (!cfg.flat_table && cfg.use_tlb) {
        cout << "TLB Hits: " << s.tlb_hits << ", Misses: " << s.tlb_misses
             << ", Hit Rate: " << fixed << setprecision(2) << s.hit_rate() << "%\n";
    } else {
        cout << "TLB: disabled\n";
    }

    cout << "Avg Translation Cost: " << fixed << setprecision(2) << s.avg_cycles()
         << " modeled cycles\n"
         << "Avg Translation Time: " << setprecision(4) << measure_avg_us(trace, cfg)
         << " μs (measured with printing off)\n";

    uint64_t flat_bytes = NUM_VPNS * sizeof(PageTableEntry);
    if (cfg.flat_table) {
        cout << "Page Table Memory: " << flat_bytes / 1024 << " KB (always)\n";
    } else {
        uint64_t two_level_bytes = sizeof(page_directory) +
            s.page_tables * PT_ENTRIES * sizeof(PageTableEntry);
        cout << "Page Table Memory: " << two_level_bytes / 1024 << " KB in " << s.page_tables
             << " tables (flat table: " << flat_bytes / 1024 << " KB)\n";
    }
}

void print_multi_run(const vector<uint32_t>& trace, const SimConfig& cfg, int runs) {
    double sum = 0.0, low = 100.0, high = 0.0;
    uint64_t fault_sum = 0;

    cout << "Run  Seed  Hit Rate  Page Faults\n";
    for (int i = 0; i < runs; i++) {
        SimConfig run_cfg = cfg;
        run_cfg.seed = cfg.seed + i;
        run_cfg.print_lines = run_cfg.print_faults = false;
        RunStats s = run_trace(trace, run_cfg);
        sum += s.hit_rate();
        low = min(low, s.hit_rate());
        high = max(high, s.hit_rate());
        fault_sum += s.page_faults;
        cout << setw(3) << i + 1 << setw(6) << run_cfg.seed << setw(9) << fixed << setprecision(2)
             << s.hit_rate() << "%" << setw(13) << s.page_faults << "\n";
    }
    cout << "\nAverage over " << runs << " runs: Hit Rate " << sum / runs << "% (min " << low
         << "%, max " << high << "%), Page Faults " << setprecision(1)
         << static_cast<double>(fault_sum) / runs << "\n";
}

struct AverageResult {
    double hit_rate = 0.0, avg_cycles = 0.0, page_faults = 0.0, fault_rate = 0.0;
};

AverageResult average_runs(const vector<uint32_t>& trace, SimConfig cfg, int runs) {
    AverageResult avg;
    for (int i = 0; i < runs; i++) {
        cfg.seed = 42 + i;
        RunStats s = run_trace(trace, cfg);
        avg.hit_rate += s.hit_rate() / runs;
        avg.avg_cycles += s.avg_cycles() / runs;
        avg.page_faults += static_cast<double>(s.page_faults) / runs;
        avg.fault_rate += s.page_faults * 100.0 / s.translations / runs;
    }
    return avg;
}

void write_macro(ofstream& out, const string& name, double value, int digits) {
    out << "\\newcommand{\\" << name << "}{" << fixed << setprecision(digits) << value << "}\n";
}

// Runs every experiment in the report. With a csv_dir it also writes the data
// files the LaTeX report reads, so its tables and graphs always match.
void run_experiments(const vector<uint32_t>& trace, const SimConfig& base, const string& csv_dir) {
    const int RUNS = 10;
    const size_t LOCALITY_LENGTH = 20000, WORKING_SET_LENGTH = 50000;
    vector<uint32_t> locality = make_locality_trace(LOCALITY_LENGTH, base.seed);
    const string dir = csv_dir.empty() ? "." : csv_dir;

    SimConfig cfg = base;
    cfg.print_lines = cfg.print_faults = false;
    ofstream size_csv, ws_csv, macros;
    if (!csv_dir.empty()) {
        size_csv.open(dir + "/tlb_size.csv");
        ws_csv.open(dir + "/working_set.csv");
        macros.open(dir + "/metrics.tex");
        if (!size_csv || !ws_csv || !macros) {
            cerr << "ERROR: cannot write to " << dir << "\n";
            exit(1);
        }
        macros << "% Generated by ./lab --experiment. Do not edit by hand.\n";
        size_csv << "entries,trace_hit,trace_cycles,locality_hit,locality_cycles\n";
        ws_csv << "pages,hit_rate,fault_rate\n";
    }

    SimConfig no_tlb = cfg;
    no_tlb.use_tlb = false;
    AverageResult trace_base = average_runs(trace, no_tlb, RUNS);
    AverageResult locality_base = average_runs(locality, no_tlb, RUNS);

    cout << "=== Experiment: TLB Size Impact (average of " << RUNS << " runs) ===\n"
         << "                addresses.txt         locality trace\n"
         << "TLB Entries   Hit Rate   Cycles     Hit Rate   Cycles\n"
         << fixed << setprecision(2);
    cout << setw(11) << "none" << setw(11) << "-" << setw(9) << trace_base.avg_cycles
         << setw(13) << "-" << setw(9) << locality_base.avg_cycles << "\n";
    for (int entries : {4, 8, 16, 32}) {
        cfg.tlb_entries = entries;
        AverageResult t = average_runs(trace, cfg, RUNS);
        AverageResult l = average_runs(locality, cfg, RUNS);
        cout << setw(11) << entries << setw(10) << t.hit_rate << "%" << setw(9) << t.avg_cycles
             << setw(12) << l.hit_rate << "%" << setw(9) << l.avg_cycles << "\n";
        if (size_csv.is_open()) {
            size_csv << entries << "," << t.hit_rate << "," << t.avg_cycles << ","
                     << l.hit_rate << "," << l.avg_cycles << "\n";
        }
    }
    cfg.tlb_entries = base.tlb_entries;

    cout << "\n=== Experiment: Working Set Size (" << DEFAULT_TLB_ENTRIES << "-entry TLB, "
         << NUM_FRAMES << " frames, no invalidation) ===\n"
         << "Pages   TLB Hit Rate   Page Fault Rate\n";
    SimConfig ws_cfg = cfg;
    ws_cfg.invalidate_rate = 0.0;
    for (uint32_t pages : {4u, 8u, 16u, 24u, 32u, 64u, 128u, 256u, 512u, 1024u}) {
        AverageResult r = average_runs(make_working_set_trace(pages, WORKING_SET_LENGTH, base.seed),
                                       ws_cfg, RUNS);
        cout << setw(5) << pages << setw(14) << r.hit_rate << "%" << setw(17) << r.fault_rate << "%\n";
        if (ws_csv.is_open()) ws_csv << pages << "," << r.hit_rate << "," << r.fault_rate << "\n";
        if (macros.is_open() && pages == 256) write_macro(macros, "FaultRateFits", r.fault_rate, 2);
        if (macros.is_open() && pages == 512) write_macro(macros, "FaultRateDouble", r.fault_rate, 2);
        if (macros.is_open() && pages == 1024) write_macro(macros, "FaultRateQuad", r.fault_rate, 2);
    }

    double trace_us = measure_avg_us(trace, cfg), trace_base_us = measure_avg_us(trace, no_tlb);
    double local_us = measure_avg_us(locality, cfg, 20), local_base_us = measure_avg_us(locality, no_tlb, 20);
    cout << "\n=== Measured Simulator Time (printing off) ===\n"
         << "                 With TLB    Without TLB\n"
         << setprecision(4)
         << "addresses.txt  " << setw(8) << trace_us << " μs" << setw(11) << trace_base_us << " μs\n"
         << "locality trace " << setw(8) << local_us << " μs" << setw(11) << local_base_us << " μs\n";

    if (macros.is_open()) {
        RunStats single = run_trace(trace, cfg);
        AverageResult t = average_runs(trace, cfg, RUNS);
        AverageResult l = average_runs(locality, cfg, RUNS);
        SimConfig unlimited = cfg;
        unlimited.frames = UNLIMITED_FRAMES;
        unlimited.invalidate_rate = 0.0;
        write_macro(macros, "DistinctPages", static_cast<double>(run_trace(trace, unlimited).page_faults), 0);
        write_macro(macros, "MetricAddresses", static_cast<double>(trace.size()), 0);
        write_macro(macros, "MetricHitRate", single.hit_rate(), 2);
        write_macro(macros, "MetricHits", static_cast<double>(single.tlb_hits), 0);
        write_macro(macros, "MetricFreeFaults", static_cast<double>(single.page_faults - single.evictions), 0);
        write_macro(macros, "MetricFaults", static_cast<double>(single.page_faults), 0);
        write_macro(macros, "MetricEvictions", static_cast<double>(single.evictions), 0);
        write_macro(macros, "MetricInvalidations", static_cast<double>(single.invalidations), 0);
        write_macro(macros, "MetricCycles", single.avg_cycles(), 2);
        write_macro(macros, "MetricPageTables", static_cast<double>(single.page_tables), 0);
        uint64_t table_bytes = sizeof(page_directory) + single.page_tables * PT_ENTRIES * sizeof(PageTableEntry);
        write_macro(macros, "TwoLevelKB", static_cast<double>(table_bytes / 1024), 0);
        write_macro(macros, "FlatKB", static_cast<double>(NUM_VPNS * sizeof(PageTableEntry) / 1024), 0);
        double low = 100.0, high = 0.0;
        for (int i = 0; i < RUNS; i++) {
            SimConfig run_cfg = cfg;
            run_cfg.seed = 42 + i;
            double rate = run_trace(trace, run_cfg).hit_rate();
            low = min(low, rate);
            high = max(high, rate);
        }
        write_macro(macros, "MinHitRate", low, 2);
        write_macro(macros, "MaxHitRate", high, 2);
        write_macro(macros, "AvgHitRate", t.hit_rate, 2);
        write_macro(macros, "AvgFaults", t.page_faults, 1);
        write_macro(macros, "AvgCycles", t.avg_cycles, 2);
        write_macro(macros, "BaseCycles", trace_base.avg_cycles, 2);
        write_macro(macros, "LocalHitRate", l.hit_rate, 2);
        write_macro(macros, "LocalCycles", l.avg_cycles, 2);
        write_macro(macros, "LocalBaseCycles", locality_base.avg_cycles, 2);
        write_macro(macros, "TimeTLB", trace_us, 4);
        write_macro(macros, "TimeNoTLB", trace_base_us, 4);
        write_macro(macros, "LocalTimeTLB", local_us, 4);
        write_macro(macros, "LocalTimeNoTLB", local_base_us, 4);
        cout << "\nWrote tlb_size.csv, working_set.csv and metrics.tex to " << dir << "\n";
    }
}

void print_usage() {
    cout << "Usage: ./lab [options]\n"
         << "  --part N        1 direct table, 2 two-level, 3 adds TLB, 4 adds faults (default 4)\n"
         << "  --file PATH     address trace (default addresses.txt)\n"
         << "  --tlb N         number of TLB entries (default 16)\n"
         << "  --no-tlb        turn the TLB off\n"
         << "  --frames N      physical frames (default 256 in Part 4)\n"
         << "  --invalidate P  chance per access of invalidating a page (default 0.01 in Part 4)\n"
         << "  --seed N        random seed (default 42)\n"
         << "  --runs N        run N seeds and print the average hit rate\n"
         << "  --quiet         print the summary only\n"
         << "  --experiment    run the report's experiments\n"
         << "  --csv DIR       with --experiment, also write the report's data files to DIR\n"
         << "  --help          show this message\n";
}

// === Main ===
int main(int argc, char* argv[]) {
    int part = 4, runs = 1;
    int tlb_entries = DEFAULT_TLB_ENTRIES;
    long frames = -1;
    double invalidate = -1.0;
    uint32_t seed = 42;
    bool no_tlb = false, quiet = false, experiment = false;
    string path = "addresses.txt", csv_dir;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        auto value = [&]() -> string {
            if (i + 1 >= argc) {
                cerr << "ERROR: " << arg << " needs a value\n";
                exit(1);
            }
            return argv[++i];
        };
        if (arg == "--part") part = stoi(value());
        else if (arg == "--file") path = value();
        else if (arg == "--tlb") tlb_entries = stoi(value());
        else if (arg == "--no-tlb") no_tlb = true;
        else if (arg == "--frames") frames = stol(value());
        else if (arg == "--invalidate") invalidate = stod(value());
        else if (arg == "--seed") seed = static_cast<uint32_t>(stoul(value()));
        else if (arg == "--runs") runs = stoi(value());
        else if (arg == "--quiet") quiet = true;
        else if (arg == "--experiment") experiment = true;
        else if (arg == "--csv") csv_dir = value();
        else if (arg == "--help") { print_usage(); return 0; }
        else {
            cerr << "ERROR: unknown option " << arg << "\n";
            print_usage();
            return 1;
        }
    }

    if (part < 1 || part > 4 || tlb_entries < 1 || runs < 1 ||
        frames == 0 || frames > static_cast<long>(UNLIMITED_FRAMES)) {
        cerr << "ERROR: invalid option value\n";
        return 1;
    }

    SimConfig cfg;
    cfg.flat_table = part == 1;
    cfg.use_tlb = part >= 3 && !no_tlb;
    cfg.tlb_entries = tlb_entries;
    cfg.frames = part == 4 ? NUM_FRAMES : UNLIMITED_FRAMES;
    cfg.invalidate_rate = part == 4 ? 0.01 : 0.0;
    cfg.seed = seed;
    if (part != 1) {  // the flat table has no eviction, so these only apply to Parts 2-4
        if (frames > 0) cfg.frames = static_cast<uint32_t>(frames);
        if (invalidate >= 0.0) cfg.invalidate_rate = invalidate;
    }
    cfg.print_lines = !quiet;
    cfg.print_faults = !quiet && part == 4;

    vector<uint32_t> addresses;
    if (!load_addresses(path, addresses)) {
        cerr << "ERROR: cannot open " << path << " (run ./generate to create it)\n";
        return 1;
    }
    if (addresses.empty()) {
        cerr << "ERROR: " << path << " has no addresses\n";
        return 1;
    }

    if (experiment) {
        run_experiments(addresses, cfg, csv_dir);
        return 0;
    }
    if (runs > 1) {
        print_multi_run(addresses, cfg, runs);
        return 0;
    }

    const char* modes[] = {"", "direct-mapped page table", "2-level page table",
                           "2-level page table", "2-level page table"};
    cout << "Starting translation with " << modes[part];
    if (cfg.use_tlb) cout << " + TLB (" << tlb_entries << "-entry LRU)";
    if (cfg.frames < UNLIMITED_FRAMES) cout << " + " << cfg.frames << " frames";
    cout << "\n\n";

    RunStats result = run_trace(addresses, cfg);
    print_summary(addresses, result, cfg);
    return 0;
}
