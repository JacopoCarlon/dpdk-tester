#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_random.h>
#include <rte_pie.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <math.h>
#include <rte_malloc.h>
#include <stdbool.h>

#define NUM_PORTS 2
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

//#define BURST_SIZE 32
/*
Powers of 2 : 
    2^ 1 = 2
    2^ 2 = 4
    2^ 3 = 8
    2^ 4 = 16
    2^ 5 = 32
    2^ 6 = 64
    2^ 7 = 128
    2^ 8 = 256
    2^ 9 = 512
    2^ 10 = 1024
    2^ 11 = 2048
    2^ 12 = 4096
    2^ 13 = 8192
    2^ 14 = 16384
    2^ 15 = 32768
    2^ 16 = 65536
    2^ 17 = 131072
    2^ 18 = 262144
    2^ 19 = 524288
    2^ 20 = 1048576
    2^ 21 = 2097152
    2^ 22 = 4194304
    2^ 23 = 8388608
    2^ 24 = 16777216
    2^ 25 = 33554432
    2^ 26 = 67108864
    2^ 27 = 134217728
    2^ 28 = 268435456
    2^ 29 = 536870912
    2^ 30 = 1073741824
    2^ 31 = 2147483648
    2^ 32 = 4294967296
*/
#define MAX_BURST_SIZE 16384        
#define DEFAULT_BURST_SIZE 32

/*
The optimum size (in terms of memory usage) for a mempool 
  is when n is a power of two minus one: n = (2^q - 1).
*/
// For 1023 elements (2^10 - 1)
#define MBUF_POOL_SIZE ((1 << 14) - 1)

/*
If cache_size is non-zero, the rte_mempool library will try 
    to limit the accesses to the common lockless pool, 
    by maintaining a per-lcore object cache. 
    This argument must be lower or equal to RTE_MEMPOOL_CACHE_MAX_SIZE and n / 1.5. 
    It is advised to choose cache_size to have "n modulo cache_size == 0"
*/
#define MEMPOOL_CACHE_SIZE 512


#define PORT_SEND_OUT_OF 0
#define PORT_RECEIVE_ON 1

#define SEQ_NUM_OFFSET 40

#define TIMESTAMP_OFFSET 48
#define MAGIC_SIGNATURE 0x1234ABCD
//  #define MAX_LATENCY_SAMPLES 1000000

#define MAX_BINS 5000  // Each bin represents 1us up to 1ms
// Ethernet frame overhead calculations

#define PHYSICAL_OVERHEAD 24  // 4(FCS) + 8(preamble) + 12(IFG)
#define IP_HDR_SIZE 20
#define UDP_HDR_SIZE 8
#define WIRE_SIZE(pkt_size) ((pkt_size) + PHYSICAL_OVERHEAD)


#define MIN_PAYLOAD_SIZE (TIMESTAMP_OFFSET + sizeof(uint64_t))
#define MIN_PKT_SIZE (sizeof(struct rte_ether_hdr) + \
                     sizeof(struct rte_ipv4_hdr) + \
                     MIN_PAYLOAD_SIZE)

// Rate limiting state
static struct {
    uint64_t next_expected_tsc;
    uint64_t tsc_per_burst;
    uint64_t tsc_hz;
    uint64_t packets_per_sec;
} rate_state = {0};



// Traffic pattern types
typedef enum {
    PATTERN_UNIFORM,      // Constant rate (packets/sec)
    PATTERN_BURST_PAUSE,  // Burst N packets, pause T seconds
    PATTERN_SAWTOOTH,     // Rate increases in steps
    PATTERN_POISSON,      // Poisson distributed bursts
    PATTERN_RANDOM_RATE,  // Random rate changes
    PATTERN_DATACENTER,    // Data center-like traffic
    PATTERN_ON_OFF,
    PATTERN_DC_ON_OFF_LOGNORMAL,
    PATTERN_TLOGN,
    PATTERN_MULTI_EXP_LOGN,
    PATTERN_WEB,
    PATTERN_GAMING
} traffic_pattern_t;

// Traffic configuration
static struct {
    traffic_pattern_t pattern;
    union {
        struct { // Burst-Pause
            uint64_t burst_size;
            double pause_sec;
        } bp;
        struct { // Sawtooth
            uint64_t start_rate;
            uint64_t end_rate;
            double step_duration;
            uint32_t num_steps;
        } st;
        struct { // Poisson
            double lambda;         // Average rate (packets/sec)
            uint32_t burst_size;   // Packets per burst
        } poisson;
        struct { // Random Rate
            uint64_t min_rate;
            uint64_t max_rate;
            double change_interval;
        } random_rate;
        struct { // Data Center
            uint64_t base_rate;
            uint64_t burst_rate;
            double burst_duration;
            double burst_interval;
            double burst_probability;
        } dc;
        struct { // ON/OFF
            double ton;
            double toff;
            uint64_t rate;
        } onoff;
        struct { // DC ON/OFF Lognormal
            uint64_t on_rate;      // Rate during ON phases (packets/sec)
            double on_mu;          // Lognormal mu for ON duration
            double on_sigma;       // Lognormal sigma for ON duration
            double off_mu;         // Lognormal mu for OFF duration 
            double off_sigma;      // Lognormal sigma for OFF duration
        } dclognormal;

        struct { // TLOGN pattern
            double on_mu;
            double on_sigma;
            double off_mu;
            double off_sigma;
            double rate_mu;       // Lognormal mu for interarrival time (seconds)
            double rate_sigma;    // Lognormal sigma for interarrival time
            uint32_t burst_size;  // Packets per burst during ON phase
        } tlogn;

        struct {
            uint32_t num_users;      // number of independent users
            double exp_rate;          // exponential rate (events per second)
            double logn_mu;           // lognormal mu for burst size (bytes)
            double logn_sigma;        // lognormal sigma for burst size
            uint32_t min_bytes;       // minimum burst size (bytes)
            uint32_t max_bytes;       // maximum burst size (bytes)
            uint64_t target_bps;      // target aggregate bitrate (bps)
        } multi_exp_logn;

        struct {
            uint32_t num_users;
            double main_mu;
            double main_sigma;
            uint32_t main_min;
            uint32_t main_max;
            double emb_mu;
            double emb_sigma;
            uint32_t emb_min;
            uint32_t emb_max;
            double pareto_alpha;
            double pareto_k;
            double pareto_m;
            double parsing_lambda;
            double reading_lambda;
            uint64_t target_bps;        // optional, defaults to 8 Gbps
        } web;

        struct {
            uint32_t num_users;
            uint8_t direction;          // 0 = UL, 1 = DL

            // UL parameters
            double ul_initial_a;
            double ul_initial_b;         // uniform [a,b] for initial packet
            uint8_t ul_arrival_type;     // 0 = deterministic, 1 = Gumbel
            double ul_arrival_a;         // for Gumbel: a (location)
            double ul_arrival_b;         // for Gumbel: b (scale)
            double ul_size_a;
            double ul_size_b;            // Gumbel for UL packet size

            // DL parameters
            double dl_initial_a;
            double dl_initial_b;
            double dl_arrival_a;         // Gumbel for DL inter-arrival
            double dl_arrival_b;
            double dl_size_a;
            double dl_size_b;

            uint64_t target_bps;         // optional aggregate bitrate limit
        } gaming;

    } params;
    uint16_t burst_size;      // Configurable burst size
} traffic_config = {PATTERN_UNIFORM};


unsigned long long DEFAULT_TARGET_BPS = 30000000000ULL;     // default to 30Gbps

static struct rte_mempool *mbuf_pool;
static volatile bool force_quit = false;

/* MAC addresses Cplex3-Treebeard */
//  static struct rte_ether_addr src_mac_b0 = {{0x68, 0x05, 0xca, 0x95, 0xfa, 0x64}};   //cplex3 enp4s0f1np0
//  static struct rte_ether_addr src_mac_b1 = {{0x68, 0x05, 0xca, 0x95, 0xfa, 0x65}};   //cplex3 enp4s0f1np1
//  static struct rte_ether_addr dst_mac_a0 = {{0x68, 0x05, 0xca, 0x95, 0xf8, 0xec}};   //treebeard enp4s0f1np0


/* MAC addresses lace-whiskey*/
static struct rte_ether_addr src_mac_b0 = {{0x68, 0x05, 0xca, 0x34, 0x85, 0xa8}};   //lace enp4s0f1np0 i40e0
static struct rte_ether_addr src_mac_b1 = {{0x68, 0x05, 0xca, 0x34, 0x85, 0xa9}};   //lace enp4s0f1np1 i40e1
static struct rte_ether_addr dst_mac_a0 = {{0x68, 0x05, 0xca, 0x34, 0x87, 0x10}};   //whiskey enp4s0f1np0 i40e0
static struct rte_ether_addr dst_mac_a1 = {{0x68, 0x05, 0xca, 0x34, 0x87, 0x11}};   //whiskey enp4s0f1np0 i40e1


/* IP configuration */
static uint32_t dst_ip = RTE_IPV4(2, 1, 1, 100);
static uint32_t src_ip = RTE_IPV4(1, 1, 1, 100);

// Configuration parameters
static uint64_t target_rate = 0;
static uint64_t total_packets = 0;
static uint64_t PACKET_SIZE = 1000;




// --- STATISTICS --- 

// Global variables for Welford's algorithm
static double sample_mean = 0.0;
static double sample_M2 = 0.0;
static uint64_t sample_count = 0;

// Throughput tracking
static uint64_t interval_wire_bytes = 0;
static uint64_t total_wire_bytes = 0;

static uint64_t global_start_tsc;

static volatile int stats_enabled = 0;      // 0 = warmup, 1 = collecting stats
static int64_t warmup_us = -1;              // warmup duration in us 
int64_t DEFAULT_WARMUP_US = 50000;          

static struct port_stats {
    uint64_t tx;
    uint64_t rx;
    uint64_t latency_total;
    uint64_t min_latency;
    uint64_t max_latency;
    uint64_t squared_latency_sum;
    // Interval stats
    uint64_t interval_tx;
    uint64_t interval_rx;
    uint64_t interval_latency_total;
    uint64_t interval_min_latency;
    uint64_t interval_max_latency;
    uint64_t interval_squared_latency_sum;
    uint64_t histogram[MAX_BINS];
    uint64_t interval_histogram[MAX_BINS];
    uint32_t max_bin;            // Track highest bin with data
    uint32_t interval_max_bin;
    uint64_t interval_start_tsc;
#ifdef DEBUG
    rte_spinlock_t interval_lock; // Lock for interval stats
#endif
} stats;

// Overall statistics
struct overall_stats {
    uint64_t total_tx;
    uint64_t total_rx;
    uint64_t min_latency;
    uint64_t max_latency;
    double avg_latency;
    double stddev_latency;
    double p95;
    double p99;
} overall;




// --- PATTERN MANAGING --- 

// Sawtooth state
static struct {
    uint64_t start_tsc;
    uint64_t current_rate;
    uint32_t current_step;
    double rate_increment;
    uint64_t tsc_per_step;
} sawtooth_state;

// Burst-pause state
static struct {
    uint64_t burst_remaining;
    uint64_t pause_end_tsc;
    bool in_pause;
} burst_pause_state = {0, 0, false};


// Poisson state
static struct {
    uint64_t next_burst_tsc;
} poisson_state;

// Random Rate state
static struct {
    uint64_t next_change_tsc;
    uint64_t current_rate;
} random_rate_state;

// Data Center state
static struct {
    uint64_t burst_end_tsc;
    uint64_t next_burst_check;
    bool in_burst;
} dc_state;

static struct {
    uint64_t phase_end_tsc;
    bool in_on_phase;
    uint64_t tsc_hz;
    bool was_in_on_phase;
} onoff_state;

static struct {
    uint64_t phase_end_tsc;
    bool in_on_phase;
    uint64_t tsc_hz;
    double current_on_duration;
    double current_off_duration;
} dclognormal_state;


// TLOGN state
static struct {
    uint64_t phase_end_tsc;
    uint64_t next_burst_tsc;
    bool in_on_phase;
    double current_on_duration;
    double current_off_duration;
    uint64_t tsc_hz;
} tlogn_state;




// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



// --- SETUP --- 

static void setup_eth_header(struct rte_ether_hdr *eth_hdr, uint16_t port_id) {
    if (port_id == 0) {
        rte_ether_addr_copy(&src_mac_b0, &eth_hdr->src_addr);
        rte_ether_addr_copy(&dst_mac_a0, &eth_hdr->dst_addr);
    }
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


static void setup_ip_header(struct rte_ipv4_hdr *ip_hdr) {
    if (PACKET_SIZE < sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + 16) {
        rte_exit(EXIT_FAILURE, "Packet size too small for headers\n");
    }

    ip_hdr->version_ihl = 0x45;
    ip_hdr->total_length = rte_cpu_to_be_16(PACKET_SIZE - sizeof(struct rte_ether_hdr));
    ip_hdr->time_to_live = 64;
    ip_hdr->next_proto_id = IPPROTO_RAW;
    ip_hdr->src_addr = src_ip;
    ip_hdr->dst_addr = dst_ip;
    ip_hdr->hdr_checksum = 0;
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
}


// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


static void setup_payload(char *payload) {
    *(uint32_t*)payload = MAGIC_SIGNATURE;
}


// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


// --- SEND --- 

static double generate_lognormal(double mu, double sigma) {
    // Box-Muller transform for normal distribution
    double u1 = (rte_rand() + 1.0) / (RTE_RAND_MAX + 2.0);
    double u2 = (rte_rand() + 1.0) / (RTE_RAND_MAX + 2.0);
    double z0 = sqrt(-2.0 * log(u1)) * cos(2 * M_PI * u2);  
    // Transform to lognormal
    return exp(mu + sigma * z0);
}


/* Simple xorshift32 PRNG - returns a 32-bit random number */
static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}


/* Returns a uniform double in (0, 1] using the given per-user state */
static inline double rand_double_per_user(uint32_t *state) {
    return (xorshift32(state) + 1.0) / (UINT32_MAX + 2.0);
}


/* Generate lognormal(mu, sigma) using per-user RNG state */
static double generate_lognormal_per_user(uint32_t *state, double mu, double sigma) {
    double u1 = rand_double_per_user(state);
    double u2 = rand_double_per_user(state);
    double z0 = sqrt(-2.0 * log(u1)) * cos(2 * M_PI * u2);
    return exp(mu + sigma * z0);
}



/* Generate Gumbel (largest extreme value) using per-user RNG state */
static double generate_gumbel_per_user(uint32_t *state, double a, double b) {
    double u = rand_double_per_user(state);
    return a - b * log(-log(u));   // inverse CDF
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



static void update_sawtooth_rate(uint64_t tsc_hz) {
    uint64_t now = rte_rdtsc_precise();
    uint64_t elapsed = now - sawtooth_state.start_tsc;
    uint32_t current_step = (uint32_t)(elapsed / sawtooth_state.tsc_per_step);

    if (current_step >= traffic_config.params.st.num_steps) {
        sawtooth_state.start_tsc = now;
        current_step = 0;
    }

    if (current_step != sawtooth_state.current_step) {
        sawtooth_state.current_step = current_step;
        sawtooth_state.current_rate = traffic_config.params.st.start_rate + 
            (uint64_t)(sawtooth_state.rate_increment * current_step);
    }
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


// multipleExpLogn: -p multipleExpLogn num_users exp_rate mu sigma min_bytes max_bytes [bitrate]",
/* FTP-traffic : 
    -> truncated lognormal sigma 0.35; mu 14.45; exponential lambda 0.006; max 5Mbytes
    sudo ./latency_test -l 2,4,6 -- -B 128 -s 300 -p multipleExpLogn -- 350  0.006  14.45  0.35 100 5000000 25000000000
*/
// 


static int lcore_send_multiple_exp_logn(__rte_unused void *arg) {
    printf("\n\n--- This is the new one ---\n\n");
    
    const uint16_t tx_port = PORT_SEND_OUT_OF;
    uint64_t seq_num = 0;
    uint64_t tsc_hz = rte_get_tsc_hz();
    uint64_t mel_send_empty_queues = 0;
    
    // ---------- Per-pattern static state (allocated once) ----------
    static int initialized = 0;
    static struct multi_user_state {
        uint64_t next_event_time;   // TSC for next event
        uint64_t pending_packets;   // packets waiting to be sent
        uint32_t rng_state;         // per-user RNG state
    } *users = NULL;
    static uint32_t *heap = NULL;           // min-heap of user indices (by next_event_time)
    static int heap_size = 0;
    static uint32_t *active_queue = NULL;   // circular queue of users with pending packets
    static int active_head = 0, active_tail = 0;
    static bool *in_queue = NULL;            // true if user is already in active_queue
    static int num_users;
    static double exp_rate, logn_mu, logn_sigma;
    static uint32_t min_bytes, max_bytes;
    static uint64_t target_pps;              // packets per second after rate limiting
    static uint64_t tsc_per_packet;          // TSC per packet at target rate
    static uint64_t total_pending_packets = 0;
    
    // ---------- One-time initialisation ----------
    if (!initialized) {
        num_users = traffic_config.params.multi_exp_logn.num_users;
        exp_rate = traffic_config.params.multi_exp_logn.exp_rate;
        logn_mu = traffic_config.params.multi_exp_logn.logn_mu;
        logn_sigma = traffic_config.params.multi_exp_logn.logn_sigma;
        min_bytes = traffic_config.params.multi_exp_logn.min_bytes;
        max_bytes = traffic_config.params.multi_exp_logn.max_bytes;
        uint64_t target_bps = traffic_config.params.multi_exp_logn.target_bps;
        
        if (target_bps == 0) {
            target_pps = 0;
            tsc_per_packet = 0;
        } else {
            // Use wire size for accurate rate calculation
            target_pps = target_bps / (8 * WIRE_SIZE(PACKET_SIZE));
            if (target_pps == 0) {
                target_pps = 1;   // avoid division by zero
            }
            tsc_per_packet = tsc_hz / target_pps;
        }
        
        /* Allocate arrays */
        users = rte_malloc("users", num_users * sizeof(struct multi_user_state), 0);
        heap = rte_malloc("heap", num_users * sizeof(uint32_t), 0);
        active_queue = rte_malloc("activeq", num_users * sizeof(uint32_t), 0);
        in_queue = rte_malloc("in_queue", num_users * sizeof(bool), 0);
        if (!users || !heap || !active_queue || !in_queue) {
            rte_exit(EXIT_FAILURE, "Failed to allocate memory for multipleExpLogn\n");
        }
        memset(in_queue, 0, num_users * sizeof(bool));
        
        /* Initialise each user */
        uint64_t now = rte_rdtsc_precise();
        for (int i = 0; i < num_users; i++) {
            /* First event time: random exponential offset from now */
            double u = (double)rte_rand() / RTE_RAND_MAX;
            u = RTE_MAX(u, 0.000000001);
            double interval = -log(1 - u) / exp_rate;   // seconds
            users[i].next_event_time = now + (uint64_t)(interval * tsc_hz);
            users[i].pending_packets = 0;
            users[i].rng_state = (uint32_t)(rte_rand() | 1u);   // seed random, ensure non-zero
            heap[i] = i;
        }
        
        /* Build min-heap (simple insertion) */
        heap_size = num_users;
        for (int i = 1; i < num_users; i++) {
            int j = i;
            while (j > 0 && users[heap[j]].next_event_time < users[heap[(j-1)/2]].next_event_time) {
                uint32_t tmp = heap[j];
                heap[j] = heap[(j-1)/2];
                heap[(j-1)/2] = tmp;
                j = (j-1)/2;
            }
        }
        
        initialized = 1;
        //  //  printf("\nMultiple Exponential/Lognormal pattern has been initialized initialized;\n"
        //  //      "parameters : %d users, exp_interarrival(lambda=%.6f), logn_size(mu=%.6f, sigma=%.6f), bytes=[%u,%u], target_bps=%lu\n",
        //  //      num_users, exp_rate, logn_mu, logn_sigma, min_bytes, max_bytes, target_bps);

        printf("\nMultiple Exponential/Lognormal (FTP) pattern initialized with %d users, target_bps=%lu\n"
            "  Session interarrival: exponential(lambda=%.6f) (mean=%.6f s)\n"
            "  File size: lognormal(mu=%.6f, sigma=%.6f), truncated [%u, %u] bytes\n",
            num_users, target_bps,
            exp_rate, 1.0/exp_rate,
            logn_mu, logn_sigma, min_bytes, max_bytes);
            
        // end of one-time initialization
        }
        
        
        // ---------- Main send loop ----------
        uint64_t now = 0;
        while (!force_quit) {
            now = rte_rdtsc_precise();
            
            /* ---- Step 1: Process due events (if any) ---- */
        //  if (heap_size > 0 && users[heap[0]].next_event_time <= now) {
            while (heap_size > 0 && users[heap[0]].next_event_time <= now) {
                if (force_quit) {
                    break;
                }
                
                // get min value from heap
                uint32_t u = heap[0];
                
                /* Pop root from heap and re-heapify*/
                heap[0] = heap[--heap_size];
                int j = 0;
                while (1) {
                    int left = 2*j + 1;     // left child of node j-th
                    int right = 2*j + 2;    // right child of node j-th
                    int smallest = j;       // (position of) j-th
                    if (left < heap_size && users[heap[left]].next_event_time < users[heap[smallest]].next_event_time){
                        smallest = left;
                    }
                    if (right < heap_size && users[heap[right]].next_event_time < users[heap[smallest]].next_event_time){
                        smallest = right;
                    }
                    if (smallest == j) {
                        // re-heapification completed
                        break;
                    }
                    uint32_t tmp = heap[j];
                    heap[j] = heap[smallest];
                    heap[smallest] = tmp;
                    j = smallest;
                }
                
                /* Generate burst size (bytes) for this user */
                double byte_burst = generate_lognormal_per_user(&users[u].rng_state,
                    logn_mu, logn_sigma);
                    if (byte_burst < min_bytes) byte_burst = min_bytes;
                    if (byte_burst > max_bytes) byte_burst = max_bytes;
                    uint32_t packets = (uint32_t)ceil(byte_burst / PACKET_SIZE);
                    if (packets == 0) packets = 1;   // at least one packet
                    
                    users[u].pending_packets += packets;
                    total_pending_packets += packets;
                    
                    /* Add user to active queue if not already there */
                    if (!in_queue[u]) {
                in_queue[u] = true;
                active_queue[active_tail] = u;
                active_tail = (active_tail + 1) % num_users;
            }
            
            /* Schedule next event for this user */
            double u_rand = rand_double_per_user(&users[u].rng_state);
            u_rand = RTE_MAX(u_rand, 0.000000001);
            double interval = -log(1 - u_rand) / exp_rate;      // seconds
            users[u].next_event_time = now + (uint64_t)(interval * tsc_hz);
            
            /* Reinsert user into heap */
            int pos = heap_size;
            heap[heap_size++] = u;
            while (pos > 0 && users[heap[pos]].next_event_time < users[heap[(pos-1)/2]].next_event_time) {
                uint32_t tmp = heap[pos];
                heap[pos] = heap[(pos-1)/2];
                heap[(pos-1)/2] = tmp;
                pos = (pos-1)/2;
            }
        }

        /* ---- Step 2: Send up to burst_size pending packets (if any) ---- */
        if (total_pending_packets > 0) {
            uint16_t to_send = (uint16_t)RTE_MIN(traffic_config.burst_size, total_pending_packets);
            struct rte_mbuf *tx_bufs[to_send];
            if (rte_pktmbuf_alloc_bulk(mbuf_pool, tx_bufs, to_send) != 0) {
                /* Allocation failed - try again later */
                continue;
            }
            
            /* Fill packets from the active queue */
            uint16_t filled = 0;
            while (filled < to_send && active_head != active_tail) {
                uint32_t u = active_queue[active_head];
                if (users[u].pending_packets == 0) {
                    /* This user no longer has packets - remove from queue */
                    in_queue[u] = false;
                    active_head = (active_head + 1) % num_users;
                    continue;
                }
                uint32_t take = RTE_MIN(users[u].pending_packets, to_send - filled);
                for (uint32_t i = 0; i < take; i++) {
                    struct rte_mbuf *m = tx_bufs[filled + i];
                    char *data = rte_pktmbuf_append(m, PACKET_SIZE);
                    struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)data;
                    struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
                    char *payload = (char *)(ip_hdr + 1);
                    setup_eth_header(eth_hdr, tx_port);
                    setup_ip_header(ip_hdr);
                    setup_payload(payload);
                    *(uint64_t*)(payload + SEQ_NUM_OFFSET) = seq_num++;
                    /* Store the user ID in the mbuf->hash.usr field - needed if send fails */
                    m->hash.usr = u;
                }
                users[u].pending_packets -= take;
                total_pending_packets -= take;
                filled += take;
                /* If user becomes empty, it will be cleaned next time we hit it */
            }
            
            /* Set timestamp just before transmission */
            uint64_t pre_tx_tsc = rte_rdtsc_precise();
            for (int i = 0; i < filled; i++) {
                char *payload = rte_pktmbuf_mtod_offset(tx_bufs[i], char *,
                    sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
                    *(uint64_t*)(payload + TIMESTAMP_OFFSET) = pre_tx_tsc;
                }
                
                /* Transmit */
                uint16_t sent = rte_eth_tx_burst(tx_port, 0, tx_bufs, filled);
                
                /* Handle unsent packets - return them to the user's pending count */
                if (sent < filled) {
                    for (int i = sent; i < filled; i++) {
                        uint32_t u = tx_bufs[i]->hash.usr;
                        users[u].pending_packets++;
                        total_pending_packets++;
                        rte_pktmbuf_free(tx_bufs[i]);
                    }
                }
                
                /* Update statistics */
                if (stats_enabled) {
                    stats.tx += sent;
                    stats.interval_tx += sent;
                    uint64_t wire_bytes = WIRE_SIZE(PACKET_SIZE) * sent;
                    total_wire_bytes += wire_bytes;
                interval_wire_bytes += wire_bytes;
            }
            
            /* Rate limiting based on target bitrate */
            if (target_pps > 0) {
                uint64_t now_after = rte_rdtsc_precise();
                uint64_t expected_next = rate_state.next_expected_tsc + sent * tsc_per_packet;
                
                if (now_after > expected_next + tsc_per_packet * traffic_config.burst_size) {
                    /* We are far behind - reset schedule to now */
                    rate_state.next_expected_tsc = now_after;
                } else if (now_after < expected_next) {
                    /* We are ahead - wait */
                    uint64_t wait_until = expected_next;
                    while (rte_rdtsc_precise() < wait_until && !force_quit) {
                        rte_pause();
                    }
                    if (force_quit) {
                        break;
                    }
                    rate_state.next_expected_tsc = expected_next;
                } else {
                    /* Exactly on time or slightly behind - just update */
                    rate_state.next_expected_tsc = expected_next;
                }
            }
        }
        
        /* ---- Step 3: If nothing to do, pause briefly until next event ---- */
        //  if (total_pending_packets == 0 && heap_size > 0 && !force_quit) {
            //      int64_t wait = users[heap[0]].next_event_time - rte_rdtsc_precise();
            //      if (wait > 0) {
                //          uint64_t wait_until = rte_rdtsc_precise() + wait;
                //          while (rte_rdtsc_precise() < wait_until && !force_quit) {
                    //              rte_pause();
                    //          }
                    //          if (force_quit) break;
                    //      }
                    //  }
                    if (force_quit) {
                        break;
                    }
                }
                
                printf("\nI am mel_lcore_send (multipleExpLogn) and i got mel_send_empty_queues = %lu\n\n", mel_send_empty_queues);
                return 0;
            }
            
            
            
            
            
            // ---------------------------------------------------------------------------
            // ---------------------------------------------------------------------------
            // ---------------------------------------------------------------------------
            
            
/* Web-browsing simplified traffic : 
"  Web (3GPP defaults): -p web 1000 8.37 1.37 100 2000000 6.17 2.36 50 2000000 1.1 2.0 55.0 7.69 0.033 10000000000\n"
    -> 
*/
            
static int lcore_send_web_traffic(__rte_unused void *arg) {
    const uint16_t tx_port = PORT_SEND_OUT_OF;
    uint64_t seq_num = 0;
    uint64_t tsc_hz = rte_get_tsc_hz();
    uint64_t tsc_hz_local = tsc_hz;

    //  // Web model parameters from Table V (3GPP)
    //  const double main_mu = 8.37;
    //  const double main_sigma = 1.37;
    //  const uint32_t main_min = 100;
    //  const uint32_t main_max = 2000000;       // 2 MB
    //  const double emb_mu = 6.17;
    //  const double emb_sigma = 2.36;
    //  const uint32_t emb_min = 50;
    //  const uint32_t emb_max = 2000000;
    //  const double pareto_alpha = 1.1;
    //  const double pareto_k = 2.0;
    //  const double pareto_m = 55.0;
    //  const double parsing_lambda = 7.69;       // exponential rate for parsing time
    //  const double reading_lambda = 0.033;      // exponential rate for reading time

    // Web model parameters from command line
    const double main_mu = traffic_config.params.web.main_mu;
    const double main_sigma = traffic_config.params.web.main_sigma;
    const uint32_t main_min = traffic_config.params.web.main_min;
    const uint32_t main_max = traffic_config.params.web.main_max;

    const double emb_mu = traffic_config.params.web.emb_mu;
    const double emb_sigma = traffic_config.params.web.emb_sigma;
    const uint32_t emb_min = traffic_config.params.web.emb_min;
    const uint32_t emb_max = traffic_config.params.web.emb_max;

    const double pareto_alpha = traffic_config.params.web.pareto_alpha;
    const double pareto_k = traffic_config.params.web.pareto_k;
    const double pareto_m = traffic_config.params.web.pareto_m;

    const double parsing_lambda = traffic_config.params.web.parsing_lambda;
    const double reading_lambda = traffic_config.params.web.reading_lambda;

    const int num_users = traffic_config.params.web.num_users;
    const uint64_t target_bps = traffic_config.params.web.target_bps;



    // Per-user state
    enum web_wait_type {
        WEB_WAIT_READING = 0,
        WEB_WAIT_PARSING = 1
    };

    struct web_user_state {
        uint64_t next_event_time;          // time of next wait (valid when state==0)
        uint64_t pending_packets;          // packets remaining in current download
        uint32_t rng_state;                // per-user RNG
        uint8_t state;                      // 0 = waiting, 1 = downloading
        uint8_t wait_type;                  // when waiting: which wait we are in
        uint64_t post_wait_duration_tsc;    // when downloading: duration of next wait
        uint8_t post_wait_type;              // when downloading: type of next wait
    };

    static int initialized = 0;
    static struct web_user_state *users = NULL;
    static uint32_t *heap = NULL;
    static int heap_size = 0;
    static uint32_t *active_queue = NULL;
    static int active_head = 0, active_tail = 0;
    static bool *in_queue = NULL;
    static uint64_t total_pending_packets = 0;
    static uint64_t target_pps = 0;
    static uint64_t tsc_per_packet = 0;

    if (!initialized) {
        if (target_bps == 0) {
            target_pps = 0;
            tsc_per_packet = 0;
        } else {
            target_pps = target_bps / (8 * WIRE_SIZE(PACKET_SIZE));
            if (target_pps == 0) target_pps = 1;
            tsc_per_packet = tsc_hz / target_pps;
        }

        users = rte_malloc("web_users", num_users * sizeof(struct web_user_state), 0);
        heap = rte_malloc("web_heap", num_users * sizeof(uint32_t), 0);
        active_queue = rte_malloc("web_activeq", num_users * sizeof(uint32_t), 0);
        in_queue = rte_malloc("web_in_queue", num_users * sizeof(bool), 0);
        if (!users || !heap || !active_queue || !in_queue) {
            rte_exit(EXIT_FAILURE, "Failed to allocate memory for web traffic\n");
        }
        memset(in_queue, 0, num_users * sizeof(bool));

        uint64_t now = rte_rdtsc_precise();
        for (int i = 0; i < num_users; i++) {
            users[i].rng_state = (uint32_t)(rte_rand() | 1u);
            // Initial offset: exponential with reading_lambda
            double u = rand_double_per_user(&users[i].rng_state);
            u = RTE_MAX(u, 0.000000001);
            double offset = -log(1 - u) / reading_lambda;
            users[i].next_event_time = now + (uint64_t)(offset * tsc_hz);
            users[i].pending_packets = 0;
            users[i].state = 0;               // waiting
            users[i].wait_type = WEB_WAIT_READING; // start with reading, so first event triggers main
            heap[i] = i;
        }

        // Build min-heap
        heap_size = num_users;
        for (int i = 1; i < num_users; i++) {
            int j = i;
            while (j > 0 && users[heap[j]].next_event_time < users[heap[(j-1)/2]].next_event_time) {
                uint32_t tmp = heap[j];
                heap[j] = heap[(j-1)/2];
                heap[(j-1)/2] = tmp;
                j = (j-1)/2;
            }
        }

        initialized = 1;
        //  //  printf("\nWeb traffic pattern initialized with %d users, target_bps=%lu\n", num_users, target_bps);

        printf("\nWeb traffic pattern initialized with %d users, target_bps=%lu\n"
                "  Main object: lognormal(mu=%.6f, sigma=%.6f), truncated [%u, %u] bytes\n"
                "  Embedded object: lognormal(mu=%.6f, sigma=%.6f), truncated [%u, %u] bytes\n"
                "  Embedded objects per page: truncated Pareto(alpha=%.6f, k=%.6f, m=%.6f)\n"
                "  Parsing time: exponential(lambda=%.6f) (mean=%.6f s)\n"
                "  Reading time: exponential(lambda=%.6f) (mean=%.6f s)\n",
                num_users, target_bps,
                main_mu, main_sigma, main_min, main_max,
                emb_mu, emb_sigma, emb_min, emb_max,
                pareto_alpha, pareto_k, pareto_m,
                parsing_lambda, 1.0/parsing_lambda,
                reading_lambda, 1.0/reading_lambda);

        // end of one-time initialization 
    }

    uint64_t now;
    while (!force_quit) {
        now = rte_rdtsc_precise();

        // ----- Process due waiting events -----
        while (heap_size > 0 && users[heap[0]].next_event_time <= now) {
            uint32_t u = heap[0];

            // Pop root from heap
            heap[0] = heap[--heap_size];
            int j = 0;
            while (1) {
                int left = 2*j+1, right = 2*j+2, smallest = j;
                if (left < heap_size && users[heap[left]].next_event_time < users[heap[smallest]].next_event_time)
                    smallest = left;
                if (right < heap_size && users[heap[right]].next_event_time < users[heap[smallest]].next_event_time)
                    smallest = right;
                if (smallest == j) break;
                uint32_t tmp = heap[j];
                heap[j] = heap[smallest];
                heap[smallest] = tmp;
                j = smallest;
            }

            // This user was waiting; now start the appropriate download
            if (users[u].wait_type == WEB_WAIT_READING) {
                // ----- Start MAIN object download -----
                double size = generate_lognormal_per_user(&users[u].rng_state, main_mu, main_sigma);
                if (size < main_min) size = main_min;
                if (size > main_max) size = main_max;
                uint32_t packets = (uint32_t)ceil(size / PACKET_SIZE);
                if (packets == 0) packets = 1;

                users[u].pending_packets = packets;
                total_pending_packets += packets;

                // Generate parsing time
                double u_rand = rand_double_per_user(&users[u].rng_state);
                u_rand = RTE_MAX(u_rand, 0.000000001);
                double parsing_time = -log(1 - u_rand) / parsing_lambda;
                users[u].post_wait_duration_tsc = (uint64_t)(parsing_time * tsc_hz);
                users[u].post_wait_type = WEB_WAIT_PARSING;

                users[u].state = 1; // downloading

                // Add to active queue
                if (!in_queue[u]) {
                    in_queue[u] = true;
                    active_queue[active_tail] = u;
                    active_tail = (active_tail + 1) % num_users;
                }
            } else { // WEB_WAIT_PARSING
                // ----- Start EMBEDDED objects download -----
                // Generate number of embedded objects (ND)
                double u_rand = rand_double_per_user(&users[u].rng_state);
                u_rand = RTE_MAX(u_rand, 0.000000001);
                // Truncated Pareto inverse CDF
                double x = pow((pow(pareto_m, -pareto_alpha) - pow(pareto_k, -pareto_alpha)) * u_rand +
                               pow(pareto_k, -pareto_alpha), -1.0/pareto_alpha);
                int nd = (int)floor(x) - (int)pareto_k;
                if (nd < 0) nd = 0;

                double total_bytes = 0;
                for (int i = 0; i < nd; i++) {
                    double size = generate_lognormal_per_user(&users[u].rng_state, emb_mu, emb_sigma);
                    if (size < emb_min) size = emb_min;
                    if (size > emb_max) size = emb_max;
                    total_bytes += size;
                }

                uint32_t packets = (uint32_t)ceil(total_bytes / PACKET_SIZE);
                if (packets == 0 && nd > 0) packets = 1; // at least one packet if any objects

                if (packets > 0) {
                    users[u].pending_packets = packets;
                    total_pending_packets += packets;

                    // Generate reading time
                    u_rand = rand_double_per_user(&users[u].rng_state);
                    u_rand = RTE_MAX(u_rand, 0.000000001);
                    double reading_time = -log(1 - u_rand) / reading_lambda;
                    users[u].post_wait_duration_tsc = (uint64_t)(reading_time * tsc_hz);
                    users[u].post_wait_type = WEB_WAIT_READING;

                    users[u].state = 1; // downloading

                    if (!in_queue[u]) {
                        in_queue[u] = true;
                        active_queue[active_tail] = u;
                        active_tail = (active_tail + 1) % num_users;
                    }
                } else {
                    // No embedded objects → directly schedule reading wait
                    u_rand = rand_double_per_user(&users[u].rng_state);
                    u_rand = RTE_MAX(u_rand, 0.000000001);
                    double reading_time = -log(1 - u_rand) / reading_lambda;
                    users[u].next_event_time = now + (uint64_t)(reading_time * tsc_hz);
                    users[u].wait_type = WEB_WAIT_READING;
                    users[u].state = 0;

                    // Reinsert into heap
                    int pos = heap_size;
                    heap[heap_size++] = u;
                    while (pos > 0 && users[heap[pos]].next_event_time < users[heap[(pos-1)/2]].next_event_time) {
                        uint32_t tmp = heap[pos];
                        heap[pos] = heap[(pos-1)/2];
                        heap[(pos-1)/2] = tmp;
                        pos = (pos-1)/2;
                    }
                }
            }
        }

        // ----- Send packets from active queue -----
        if (total_pending_packets > 0) {
            uint16_t to_send = (uint16_t)RTE_MIN(traffic_config.burst_size, total_pending_packets);
            struct rte_mbuf *tx_bufs[to_send];
            if (rte_pktmbuf_alloc_bulk(mbuf_pool, tx_bufs, to_send) != 0) {
                continue;
            }

            uint16_t filled = 0;
            uint32_t completed[to_send]; // users that finished in this burst
            uint32_t completed_count = 0;

            while (filled < to_send && active_head != active_tail) {
                uint32_t u = active_queue[active_head];
                if (users[u].pending_packets == 0) {
                    in_queue[u] = false;
                    active_head = (active_head + 1) % num_users;
                    continue;
                }
                uint32_t take = RTE_MIN(users[u].pending_packets, to_send - filled);
                for (uint32_t i = 0; i < take; i++) {
                    struct rte_mbuf *m = tx_bufs[filled + i];
                    char *data = rte_pktmbuf_append(m, PACKET_SIZE);
                    struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)data;
                    struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
                    char *payload = (char *)(ip_hdr + 1);
                    setup_eth_header(eth_hdr, tx_port);
                    setup_ip_header(ip_hdr);
                    setup_payload(payload);
                    *(uint64_t*)(payload + SEQ_NUM_OFFSET) = seq_num++;
                    m->hash.usr = u;
                }
                users[u].pending_packets -= take;
                total_pending_packets -= take;
                if (users[u].pending_packets == 0) {
                    completed[completed_count++] = u;
                }
                filled += take;
            }

            // Set timestamps just before transmission
            uint64_t pre_tx_tsc = rte_rdtsc_precise();
            for (int i = 0; i < filled; i++) {
                char *payload = rte_pktmbuf_mtod_offset(tx_bufs[i], char *,
                            sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
                *(uint64_t*)(payload + TIMESTAMP_OFFSET) = pre_tx_tsc;
            }

            uint16_t sent = rte_eth_tx_burst(tx_port, 0, tx_bufs, filled);
            if (sent < filled) {
                for (int i = sent; i < filled; i++) {
                    uint32_t u = tx_bufs[i]->hash.usr;
                    users[u].pending_packets++;
                    total_pending_packets++;
                    rte_pktmbuf_free(tx_bufs[i]);
                }
            }

            if (stats_enabled) {
                stats.tx += sent;
                stats.interval_tx += sent;
                uint64_t wire_bytes = WIRE_SIZE(PACKET_SIZE) * sent;
                total_wire_bytes += wire_bytes;
                interval_wire_bytes += wire_bytes;
            }

            // Rate limiting (same as FTP)
            if (target_pps > 0) {
                uint64_t now_after = rte_rdtsc_precise();
                uint64_t expected_next = rate_state.next_expected_tsc + sent * tsc_per_packet;
                if (now_after > expected_next + tsc_per_packet * traffic_config.burst_size) {
                    rate_state.next_expected_tsc = now_after;
                } else if (now_after < expected_next) {
                    uint64_t wait_until = expected_next;
                    while (rte_rdtsc_precise() < wait_until && !force_quit) {
                        rte_pause();
                    }
                    if (force_quit) break;
                    rate_state.next_expected_tsc = expected_next;
                } else {
                    rate_state.next_expected_tsc = expected_next;
                }
            }

            // ----- Schedule next waits for completed users -----
            uint64_t now_after = rte_rdtsc_precise(); // use consistent time
            for (uint32_t i = 0; i < completed_count; i++) {
                uint32_t u = completed[i];
                users[u].next_event_time = now_after + users[u].post_wait_duration_tsc;
                users[u].wait_type = users[u].post_wait_type;
                users[u].state = 0; // waiting

                // Reinsert into heap
                int pos = heap_size;
                heap[heap_size++] = u;
                while (pos > 0 && users[heap[pos]].next_event_time < users[heap[(pos-1)/2]].next_event_time) {
                    uint32_t tmp = heap[pos];
                    heap[pos] = heap[(pos-1)/2];
                    heap[(pos-1)/2] = tmp;
                    pos = (pos-1)/2;
                }
            }
        }

        if (force_quit) break;
    }

    return 0;
}






// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------







static int lcore_send_game_traffic(__rte_unused void *arg) {
    const uint16_t tx_port = PORT_SEND_OUT_OF;
    uint64_t seq_num = 0;
    uint64_t tsc_hz = rte_get_tsc_hz();

    // Per-user state
    struct game_user_state {
        uint64_t next_send_time;   // TSC for next packet generation
        uint32_t rng_state;        // per-user RNG
        uint8_t pending;            // 1 if packet ready to send, else 0
    };

    static int initialized = 0;
    static struct game_user_state *users = NULL;
    static uint32_t *heap = NULL;           // min-heap of user indices (users with pending == 0)
    static int heap_size = 0;
    static uint32_t *active_queue = NULL;   // circular queue of users with pending == 1
    static int active_head = 0, active_tail = 0;
    static bool *in_queue = NULL;           // true if user already in active_queue
    static uint64_t total_pending_packets = 0;

    // Gaming parameters (read from config)
    static uint32_t num_users;
    static uint8_t direction;               // 0 = UL, 1 = DL
    static double ul_initial_a, ul_initial_b;
    static uint8_t ul_arrival_type;          // 0 = deterministic, 1 = Gumbel
    static double ul_arrival_a, ul_arrival_b;
    static double ul_size_a, ul_size_b;
    static double dl_initial_a, dl_initial_b;
    static double dl_arrival_a, dl_arrival_b;
    static double dl_size_a, dl_size_b;
    static uint64_t target_pps = 0;
    static uint64_t tsc_per_packet = 0;

    if (!initialized) {
        num_users = traffic_config.params.gaming.num_users;
        direction = traffic_config.params.gaming.direction;

        ul_initial_a = traffic_config.params.gaming.ul_initial_a;
        ul_initial_b = traffic_config.params.gaming.ul_initial_b;
        ul_arrival_type = traffic_config.params.gaming.ul_arrival_type;
        ul_arrival_a = traffic_config.params.gaming.ul_arrival_a;
        ul_arrival_b = traffic_config.params.gaming.ul_arrival_b;
        ul_size_a = traffic_config.params.gaming.ul_size_a;
        ul_size_b = traffic_config.params.gaming.ul_size_b;

        dl_initial_a = traffic_config.params.gaming.dl_initial_a;
        dl_initial_b = traffic_config.params.gaming.dl_initial_b;
        dl_arrival_a = traffic_config.params.gaming.dl_arrival_a;
        dl_arrival_b = traffic_config.params.gaming.dl_arrival_b;
        dl_size_a = traffic_config.params.gaming.dl_size_a;
        dl_size_b = traffic_config.params.gaming.dl_size_b;

        uint64_t target_bps = traffic_config.params.gaming.target_bps;
        if (target_bps == 0) {
            target_pps = 0;
            tsc_per_packet = 0;
        } else {
            // With variable packet sizes, the rate limiter based on packets is inaccurate.
            // We keep it simple: use the configured PACKET_SIZE (constant) for pps calculation.
            // This yields an approximate bitrate. For accurate byte-based limiting, a more
            // sophisticated approach would be needed.
            target_pps = target_bps / (8 * WIRE_SIZE(PACKET_SIZE));
            if (target_pps == 0) target_pps = 1;
            tsc_per_packet = tsc_hz / target_pps;
        }

        // Allocate arrays
        users = rte_malloc("game_users", num_users * sizeof(struct game_user_state), 0);
        heap = rte_malloc("game_heap", num_users * sizeof(uint32_t), 0);
        active_queue = rte_malloc("game_activeq", num_users * sizeof(uint32_t), 0);
        in_queue = rte_malloc("game_in_queue", num_users * sizeof(bool), 0);
        if (!users || !heap || !active_queue || !in_queue) {
            rte_exit(EXIT_FAILURE, "Failed to allocate memory for gaming traffic\n");
        }
        memset(in_queue, 0, num_users * sizeof(bool));

        uint64_t now = rte_rdtsc_precise();
        for (uint32_t i = 0; i < num_users; i++) {
            users[i].rng_state = (uint32_t)(rte_rand() | 1u);

            // Initial offset: uniform distribution
            double u = rand_double_per_user(&users[i].rng_state);
            double offset;
            if (direction == 0) { // UL
                offset = ul_initial_a + u * (ul_initial_b - ul_initial_a);
            } else { // DL
                offset = dl_initial_a + u * (dl_initial_b - dl_initial_a);
            }
            users[i].next_send_time = now + (uint64_t)(offset * tsc_hz);
            users[i].pending = 0;
            heap[i] = i;
        }

        // Build min-heap
        heap_size = num_users;
        for (int i = 1; i < heap_size; i++) {
            int j = i;
            while (j > 0 && users[heap[j]].next_send_time < users[heap[(j-1)/2]].next_send_time) {
                uint32_t tmp = heap[j];
                heap[j] = heap[(j-1)/2];
                heap[(j-1)/2] = tmp;
                j = (j-1)/2;
            }
        }

        initialized = 1;
        printf("\nGaming traffic pattern initialized with %u users, direction %s, target_bps=%lu\n"
               "  UL: initial=[%.3f,%.3f]s, arrival_type=%s, inter-arrival=(%.3f,%.3f), size=(%.1f,%.1f)\n"
               "  DL: initial=[%.3f,%.3f]s, inter-arrival=(%.3f,%.3f), size=(%.1f,%.1f)\n",
               num_users, (direction == 0 ? "UL" : "DL"), target_bps,
               ul_initial_a, ul_initial_b, ul_arrival_type ? "Gumbel" : "Deterministic",
               ul_arrival_a, ul_arrival_b, ul_size_a, ul_size_b,
               dl_initial_a, dl_initial_b, dl_arrival_a, dl_arrival_b, dl_size_a, dl_size_b);
    }

    uint64_t now;
    while (!force_quit) {
        now = rte_rdtsc_precise();

        // ----- Process due events: users whose next_send_time <= now -----
        while (heap_size > 0 && users[heap[0]].next_send_time <= now) {
            uint32_t u = heap[0];

            // Pop root from heap
            heap[0] = heap[--heap_size];
            int j = 0;
            while (1) {
                int left = 2*j+1, right = 2*j+2, smallest = j;
                if (left < heap_size && users[heap[left]].next_send_time < users[heap[smallest]].next_send_time)
                    smallest = left;
                if (right < heap_size && users[heap[right]].next_send_time < users[heap[smallest]].next_send_time)
                    smallest = right;
                if (smallest == j) break;
                uint32_t tmp = heap[j];
                heap[j] = heap[smallest];
                heap[smallest] = tmp;
                j = smallest;
            }

            // This user now has a packet ready
            users[u].pending = 1;
            total_pending_packets++;

            // Add to active queue if not already there
            if (!in_queue[u]) {
                in_queue[u] = true;
                active_queue[active_tail] = u;
                active_tail = (active_tail + 1) % num_users;
            }
        }

        // ----- Send up to burst_size pending packets -----
        if (total_pending_packets > 0) {
            uint16_t to_send = (uint16_t)RTE_MIN(traffic_config.burst_size, total_pending_packets);
            struct rte_mbuf *tx_bufs[to_send];

            if (rte_pktmbuf_alloc_bulk(mbuf_pool, tx_bufs, to_send) != 0) {
                continue;   // allocation failed, try again later
            }

            uint16_t filled = 0;
            // We'll store the user id and the generated packet size for each mbuf
            uint32_t user_ids[to_send];
            uint16_t pkt_sizes[to_send];

            while (filled < to_send && active_head != active_tail) {
                uint32_t u = active_queue[active_head];
                if (users[u].pending == 0) {
                    // Should not happen, but remove from queue if somehow pending cleared
                    in_queue[u] = false;
                    active_head = (active_head + 1) % num_users;
                    continue;
                }
                // Take this user
                active_head = (active_head + 1) % num_users;
                in_queue[u] = false;   // will be re-added later if packet fails
                user_ids[filled] = u;

                // Generate packet size (IP packet length) using Gumbel
                double size;
                if (direction == 0) { // UL
                    size = generate_gumbel_per_user(&users[u].rng_state, ul_size_a, ul_size_b);
                } else { // DL
                    size = generate_gumbel_per_user(&users[u].rng_state, dl_size_a, dl_size_b);
                }
                // Ensure IP packet is large enough to hold our metadata
                uint16_t min_ip_len = MIN_PKT_SIZE - sizeof(struct rte_ether_hdr);  // 76 bytes
                if (size < min_ip_len) {
                    size = min_ip_len;
                }

                uint16_t ip_len = (uint16_t)size;
                uint16_t eth_len = ip_len + sizeof(struct rte_ether_hdr);

                // Prepare mbuf
                struct rte_mbuf *m = tx_bufs[filled];
                char *data = rte_pktmbuf_append(m, eth_len);
                if (!data) {
                    // Not enough space in mbuf - this should not happen if mbufs are large enough
                    rte_pktmbuf_free(m);
                    // Skip this packet, user remains pending
                    users[u].pending = 1; // still pending
                    total_pending_packets++; // we will try again later
                    // Put user back into active queue? We'll handle after this loop.
                    // For now, we just skip filling this user.
                    continue;
                }

                struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)data;
                struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
                char *payload = (char *)(ip_hdr + 1);

                setup_eth_header(eth_hdr, tx_port);

                // Fill IP header with correct total length
                ip_hdr->version_ihl = 0x45;
                ip_hdr->total_length = rte_cpu_to_be_16(ip_len);
                ip_hdr->time_to_live = 64;
                ip_hdr->next_proto_id = IPPROTO_RAW;
                ip_hdr->src_addr = src_ip;
                ip_hdr->dst_addr = dst_ip;
                ip_hdr->hdr_checksum = 0;
                ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);

                setup_payload(payload);
                *(uint64_t*)(payload + SEQ_NUM_OFFSET) = seq_num++;
                m->hash.usr = u;   // store user id for later
                pkt_sizes[filled] = eth_len; // store for timestamp and stats

                filled++;
            }

            if (filled == 0) {
                // No packets could be prepared - free all allocated bufs
                for (int i = 0; i < to_send; i++) rte_pktmbuf_free(tx_bufs[i]);
                continue;
            }

            // Set timestamp just before transmission
            uint64_t pre_tx_tsc = rte_rdtsc_precise();
            for (int i = 0; i < filled; i++) {
                char *payload = rte_pktmbuf_mtod_offset(tx_bufs[i], char *,
                            sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
                *(uint64_t*)(payload + TIMESTAMP_OFFSET) = pre_tx_tsc;
            }

            uint16_t sent = rte_eth_tx_burst(tx_port, 0, tx_bufs, filled);

            // Handle sent packets
            for (int i = 0; i < sent; i++) {
                uint32_t u = tx_bufs[i]->hash.usr;
                users[u].pending = 0;   // packet successfully sent

                // Compute next send time for this user
                uint64_t interval_tsc;
                if (direction == 0) { // UL
                    if (ul_arrival_type == 0) { // deterministic
                        interval_tsc = (uint64_t)(ul_arrival_a * tsc_hz);
                    } else { // Gumbel
                        double interval = generate_gumbel_per_user(&users[u].rng_state,
                                                    ul_arrival_a, ul_arrival_b);
                        interval_tsc = (uint64_t)(interval * tsc_hz);
                    }
                } else { // DL always Gumbel
                    double interval = generate_gumbel_per_user(&users[u].rng_state,
                                                dl_arrival_a, dl_arrival_b);
                    interval_tsc = (uint64_t)(interval * tsc_hz);
                }
                users[u].next_send_time = pre_tx_tsc + interval_tsc;

                // Reinsert into heap
                int pos = heap_size;
                heap[heap_size++] = u;
                while (pos > 0 && users[heap[pos]].next_send_time < users[heap[(pos-1)/2]].next_send_time) {
                    uint32_t tmp = heap[pos];
                    heap[pos] = heap[(pos-1)/2];
                    heap[(pos-1)/2] = tmp;
                    pos = (pos-1)/2;
                }
            }

            // Handle unsent packets (if any)
            if (sent < filled) {
                for (int i = sent; i < filled; i++) {
                    uint32_t u = tx_bufs[i]->hash.usr;
                    // User still has packet pending, put back into active queue
                    if (!in_queue[u]) {
                        in_queue[u] = true;
                        active_queue[active_tail] = u;
                        active_tail = (active_tail + 1) % num_users;
                    }
                    rte_pktmbuf_free(tx_bufs[i]);
                }
                // total_pending_packets remains same for unsent; we decrease only for sent
            }

            // Update total pending packets (only those successfully sent are removed)
            total_pending_packets -= sent;

            // Update statistics - note that we now have variable packet sizes,
            // so we need to account for the actual wire bytes.
            if (stats_enabled) {
                stats.tx += sent;
                stats.interval_tx += sent;
                uint64_t wire_bytes = 0;
                for (int i = 0; i < sent; i++) {
                    wire_bytes += WIRE_SIZE(pkt_sizes[i] - sizeof(struct rte_ether_hdr));
                }
                total_wire_bytes += wire_bytes;
                interval_wire_bytes += wire_bytes;
            }

            // Rate limiting based on target bitrate (if specified)
            if (target_pps > 0) {
                uint64_t now_after = rte_rdtsc_precise();
                uint64_t expected_next = rate_state.next_expected_tsc + sent * tsc_per_packet;
                if (now_after > expected_next + tsc_per_packet * traffic_config.burst_size) {
                    rate_state.next_expected_tsc = now_after;
                } else if (now_after < expected_next) {
                    uint64_t wait_until = expected_next;
                    while (rte_rdtsc_precise() < wait_until && !force_quit) {
                        rte_pause();
                    }
                    if (force_quit) break;
                    rate_state.next_expected_tsc = expected_next;
                } else {
                    rate_state.next_expected_tsc = expected_next;
                }
            }
        }

        if (force_quit) break;
    }

    return 0;
}


/*
First table (paper) - UL:
sudo ./latency_test -l 2,4,6 -- -B 32 -p gaming 1 ul 0.0 0.04 0 0.04 0.0 45.0 5.7 0.0 0.04 0.055 0.006 120.0 36.0
sudo ./latency_test -l 2,4,6 -- -B 32 -p gaming 1000 ul 0.0 0.04 0 0.04 0.0 45.0 5.7 0.0 0.04 0.055 0.006 120.0 36.0 10000000000


First table - DL:
sudo ./latency_test -l 2,4,6 -- -B 32 -p gaming 1 dl 0.0 0.04 0 0.04 0.0 45.0 5.7 0.0 0.04 0.055 0.006 120.0 36.0



Second table (3GPP) - UL (Gumbel arrival):
sudo ./latency_test -l 2,4,6 -- -B 32 -p gaming 1 ul 0.0 0.04 1 0.04 0.006 45.0 5.7 0.0 0.04 0.05 0.0045 330.0 82.0


Second table - DL:
sudo ./latency_test -l 2,4,6 -- -B 32 -p gaming 1 dl 0.0 0.04 1 0.04 0.006 45.0 5.7 0.0 0.04 0.05 0.0045 330.0 82.0
*/



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------







static int lcore_send(__rte_unused void *arg) {
    const uint16_t tx_port = PORT_SEND_OUT_OF;
    struct rte_mbuf *tx_bufs[MAX_BURST_SIZE];
    uint64_t seq_num = 0;
    uint64_t tsc_hz = rte_get_tsc_hz();
    double tsc_per_packet = 0;
    uint64_t start_tsc, end_tsc, target_end_tsc;
    bool do_rate_limiting = false;
    uint64_t burst_to_send;
    uint64_t packet_interval_tsc;

    uint64_t send_empty_queues = 0;

    printf("--- --- --- Starting send loop on lcore %u --- --- ---\n\n\n", rte_lcore_id());

    // Initialize sawtooth if needed
    if (traffic_config.pattern == PATTERN_SAWTOOTH) {
        sawtooth_state.start_tsc = rte_rdtsc_precise();
        sawtooth_state.tsc_per_step = 
            (uint64_t)(traffic_config.params.st.step_duration * tsc_hz);
        sawtooth_state.rate_increment = 
            (double)(traffic_config.params.st.end_rate - traffic_config.params.st.start_rate) /
            (traffic_config.params.st.num_steps - 1);
        sawtooth_state.current_step = 0;
        sawtooth_state.current_rate = traffic_config.params.st.start_rate;
    }

    // Initialize poisson state
    if (traffic_config.pattern == PATTERN_POISSON) {
        poisson_state.next_burst_tsc = rte_rdtsc_precise();
        printf("Poisson traffic pattern initialized with lambda = %.4f pkts/sec, burst size = %u\n", 
               traffic_config.params.poisson.lambda, 
               traffic_config.params.poisson.burst_size);
    }

    // Initialize random rate state
    if (traffic_config.pattern == PATTERN_RANDOM_RATE) {
        random_rate_state.next_change_tsc = rte_rdtsc_precise();
        random_rate_state.current_rate = traffic_config.params.random_rate.min_rate;
        printf("Random rate traffic pattern initialized min=%lu, max=%lu, interval=%.4fs\n",
               traffic_config.params.random_rate.min_rate,
               traffic_config.params.random_rate.max_rate,
               traffic_config.params.random_rate.change_interval);
    }

    // Initialize datacenter traffic pattern
    if (traffic_config.pattern == PATTERN_DATACENTER) {
        dc_state.next_burst_check = rte_rdtsc_precise();
        dc_state.in_burst = false;
        traffic_config.params.dc.burst_probability = 0.1; // Default probability if not set
        printf("Datacenter traffic pattern initialized base=%lu, burst=%lu\n",
               traffic_config.params.dc.base_rate,
               traffic_config.params.dc.burst_rate);
    }

    if (traffic_config.pattern == PATTERN_ON_OFF) {
        onoff_state.tsc_hz = tsc_hz;
        onoff_state.in_on_phase = true;
        onoff_state.phase_end_tsc = rte_rdtsc_precise() + 
            (uint64_t)(traffic_config.params.onoff.ton * tsc_hz);
        onoff_state.was_in_on_phase = true;             // track onoff phase !!
        printf("ON/OFF pattern: ton=%.4fs, toff=%.4fs, rate=%lu\n",
            traffic_config.params.onoff.ton,
            traffic_config.params.onoff.toff,
            traffic_config.params.onoff.rate);
    }

    if (traffic_config.pattern == PATTERN_DC_ON_OFF_LOGNORMAL) {
        dclognormal_state.tsc_hz = tsc_hz;
        dclognormal_state.in_on_phase = true;
        
        // Generate initial durations
        dclognormal_state.current_on_duration = generate_lognormal(
            traffic_config.params.dclognormal.on_mu,
            traffic_config.params.dclognormal.on_sigma
        );
        
        dclognormal_state.current_off_duration = generate_lognormal(
            traffic_config.params.dclognormal.off_mu,
            traffic_config.params.dclognormal.off_sigma
        );
        
        dclognormal_state.phase_end_tsc = rte_rdtsc_precise() + 
            (uint64_t)(dclognormal_state.current_on_duration * tsc_hz);
        
        printf("DC ON/OFF Lognormal pattern initialized:\n"
               "  ON Rate: %lu pps\n"
               "  ON Mu: %.6f, Sigma: %.6f\n"
               "  OFF Mu: %.6f, Sigma: %.6f\n",
               traffic_config.params.dclognormal.on_rate,
               traffic_config.params.dclognormal.on_mu,
               traffic_config.params.dclognormal.on_sigma,
               traffic_config.params.dclognormal.off_mu,
               traffic_config.params.dclognormal.off_sigma);

        double mean_on_dc = exp(traffic_config.params.dclognormal.on_mu + 
                                traffic_config.params.dclognormal.on_sigma * traffic_config.params.dclognormal.on_sigma / 2.0);
        double mean_off_dc = exp(traffic_config.params.dclognormal.off_mu + 
                                traffic_config.params.dclognormal.off_sigma * traffic_config.params.dclognormal.off_sigma / 2.0);

        printf("  Mean ON duration  = %.6f s (%10.2f us)\n", mean_on_dc, mean_on_dc * 1e6);
        printf("  Mean OFF duration = %.6f s (%10.2f us)\n", mean_off_dc, mean_off_dc * 1e6);
        printf("  (Formula: mean = exp(mu + sigma^2/2))\n");
    }

    if (traffic_config.pattern == PATTERN_TLOGN) {
        tlogn_state.tsc_hz = tsc_hz;
        tlogn_state.in_on_phase = true;
        
        // Generate initial durations
        tlogn_state.current_on_duration = generate_lognormal(
            traffic_config.params.tlogn.on_mu,
            traffic_config.params.tlogn.on_sigma
        );

        tlogn_state.current_off_duration = generate_lognormal(
            traffic_config.params.tlogn.off_mu,
            traffic_config.params.tlogn.off_sigma
        );

        tlogn_state.phase_end_tsc = rte_rdtsc_precise() + 
            (uint64_t)(tlogn_state.current_on_duration * tsc_hz);

        // Schedule first burst immediately
        tlogn_state.next_burst_tsc = rte_rdtsc_precise();

        printf("TLOGN pattern initialized:\n"
               "  ON Mu: %.6f, Sigma: %.6f\n"
               "  OFF Mu: %.6f, Sigma: %.6f\n"
               "  Interarrival Mu: %.6f, Sigma: %.6f\n"
               "  Burst Size: %u\n",
               traffic_config.params.tlogn.on_mu,
               traffic_config.params.tlogn.on_sigma,
               traffic_config.params.tlogn.off_mu,
               traffic_config.params.tlogn.off_sigma,
               traffic_config.params.tlogn.rate_mu,
               traffic_config.params.tlogn.rate_sigma,
               traffic_config.params.tlogn.burst_size
            );

        double mean_on = exp(traffic_config.params.tlogn.on_mu + 
                            traffic_config.params.tlogn.on_sigma * traffic_config.params.tlogn.on_sigma / 2.0);
        double mean_off = exp(traffic_config.params.tlogn.off_mu + 
                            traffic_config.params.tlogn.off_sigma * traffic_config.params.tlogn.off_sigma / 2.0);
        double mean_inter = exp(traffic_config.params.tlogn.rate_mu + 
                                traffic_config.params.tlogn.rate_sigma * traffic_config.params.tlogn.rate_sigma / 2.0);
        double duty_cycle = (mean_off > 0) ? mean_on / (mean_on + mean_off) : 1.0;
        double pps_on = traffic_config.params.tlogn.burst_size / mean_inter;
        double pps_long = pps_on * duty_cycle;

        printf("  Mean ON duration  = %.6f s (%10.2f us)\n", mean_on, mean_on * 1e6);
        printf("  Mean OFF duration = %.6f s (%10.2f us)\n", mean_off, mean_off * 1e6);
        printf("  Mean inter-burst  = %.6f s (%10.2f us)\n", mean_inter, mean_inter * 1e6);
        printf("  Expected packet rate (ON phase) = %.1f pps\n", pps_on);
        printf("  Long-term average packet rate   = %.1f pps\n", pps_long);
        printf("  (Formula: mean = exp(mu + sigma^2/2))\n");
    
    }

    if (traffic_config.pattern == PATTERN_MULTI_EXP_LOGN) {
        return lcore_send_multiple_exp_logn(arg);
    }

    if (traffic_config.pattern == PATTERN_WEB) {
        return lcore_send_web_traffic(arg);
    }

    if (traffic_config.pattern == PATTERN_GAMING) {
        return lcore_send_game_traffic(arg);
    }

    while (!force_quit) {
        // Check packet limit
        if (total_packets > 0 && stats.tx >= total_packets) {
            force_quit = true;
            break;
        }

        burst_to_send = 0;              // each pattern shall set this value if needed !!!
        bool custom_burst = false; 
        do_rate_limiting = false;

        // Update rate based on pattern
        switch (traffic_config.pattern) {
            case PATTERN_SAWTOOTH:
                update_sawtooth_rate(tsc_hz);
                target_rate = sawtooth_state.current_rate;
                tsc_per_packet = (target_rate > 0) ? (double)tsc_hz / target_rate : 0;
                do_rate_limiting = true;
                break;

            case PATTERN_BURST_PAUSE:
                if (burst_pause_state.in_pause) {
                    if (rte_rdtsc_precise() >= burst_pause_state.pause_end_tsc) {
                        burst_pause_state.in_pause = false;
                        burst_pause_state.burst_remaining = traffic_config.params.bp.burst_size;
                        start_tsc = rte_rdtsc_precise();  // Start timing for burst
                    } else {
                        continue;
                    }
                } else {
                    if (burst_pause_state.burst_remaining == 0) {
                        burst_pause_state.in_pause = true;
                        burst_pause_state.pause_end_tsc = rte_rdtsc_precise() + 
                                (uint64_t)(traffic_config.params.bp.pause_sec * tsc_hz);
                        continue;
                    }
                }
                break;

            case PATTERN_POISSON: {
                uint64_t now = rte_rdtsc_precise();
                if (now >= poisson_state.next_burst_tsc) {
                    // Calculate next burst time using exponential distribution
                    double u = (double)rte_rand() / (double)RTE_RAND_MAX;
                    u = RTE_MAX(u, 0.000000001);  // Avoid log(0)
                    double interval = -log(1 - u) / traffic_config.params.poisson.lambda;
                    // Ensure we don't get a zero interval
                    if (interval < 0.000001){
                        interval = 0.000001;
                    }
                    poisson_state.next_burst_tsc = now + (uint64_t)(interval * tsc_hz);
                    burst_to_send = traffic_config.params.poisson.burst_size;
                    start_tsc = now;  // Start timing
                } else {
                    //burst_to_send = 0;
                    // Skip this iteration if no burst to send
                    continue;  
                }
                custom_burst = true;
                break;
            }

            case PATTERN_RANDOM_RATE: {
                uint64_t now = rte_rdtsc_precise();
                if (now >= random_rate_state.next_change_tsc) {
                    // Generate new random rate between min and max
                    double t = (double)rte_rand() / (double)RTE_RAND_MAX;
                    random_rate_state.current_rate = 
                        traffic_config.params.random_rate.min_rate + 
                        (uint64_t)(t * (traffic_config.params.random_rate.max_rate - 
                                        traffic_config.params.random_rate.min_rate));
                    random_rate_state.next_change_tsc = now + 
                        (uint64_t)(traffic_config.params.random_rate.change_interval * tsc_hz);
                }
                target_rate = random_rate_state.current_rate;
                tsc_per_packet = (target_rate > 0) ? (double)tsc_hz / target_rate : 0;
                do_rate_limiting = true;
                break;
            }

            case PATTERN_DATACENTER: {
                uint64_t now = rte_rdtsc_precise();
                if (now >= dc_state.next_burst_check) {
                    // Check for burst probability
                    double burst_rand = (double)rte_rand() / (double)RTE_RAND_MAX;
                    
                    if (!dc_state.in_burst && burst_rand < traffic_config.params.dc.burst_probability) {
                        dc_state.in_burst = true;
                        dc_state.burst_end_tsc = now + 
                            (uint64_t)(traffic_config.params.dc.burst_duration * tsc_hz);
                        target_rate = traffic_config.params.dc.burst_rate;
                    } else if (!dc_state.in_burst) {
                        dc_state.next_burst_check = now + 
                            (uint64_t)(traffic_config.params.dc.burst_interval * tsc_hz);
                    }
                }
                
                // If in burst and time expired, return to base rate
                if (dc_state.in_burst && now >= dc_state.burst_end_tsc) {
                    dc_state.in_burst = false;
                    target_rate = traffic_config.params.dc.base_rate;
                    dc_state.next_burst_check = now + 
                    (uint64_t)(traffic_config.params.dc.burst_interval * tsc_hz);
                }
                
                tsc_per_packet = (target_rate > 0) ? (double)tsc_hz / target_rate : 0;
                do_rate_limiting = true;
                break;
            }

            case PATTERN_ON_OFF: {
                uint64_t now = rte_rdtsc_precise();

                // Phase transition check
                if (now >= onoff_state.phase_end_tsc) {
                    onoff_state.in_on_phase = !onoff_state.in_on_phase;
                    double duration = onoff_state.in_on_phase ? 
                        traffic_config.params.onoff.ton : 
                        traffic_config.params.onoff.toff;
                    onoff_state.phase_end_tsc = now + (uint64_t)(duration * onoff_state.tsc_hz);
                }

                if (onoff_state.in_on_phase) {
                    // Detect OFF → ON transition
                    if (!onoff_state.was_in_on_phase) {
                        // Reset rate limiter state to current time (no backlog)
                        rate_state.next_expected_tsc = now;
                        // Optionally reset other rate_state fields if needed
                        // (tsc_per_burst will be recalculated below)
                    }
                    onoff_state.was_in_on_phase = true;

                    target_rate = traffic_config.params.onoff.rate;
                    if (target_rate == 0) {
                        tsc_per_packet = 0; // send as fast as possible
                    } else {
                        tsc_per_packet = (double)onoff_state.tsc_hz / target_rate;
                    }
                    do_rate_limiting = true;
                } else {
                    onoff_state.was_in_on_phase = false;
                    target_rate = 0;
                    tsc_per_packet = 0;
                    do_rate_limiting = false;
                    continue;
                }
                break;
            }

            case PATTERN_DC_ON_OFF_LOGNORMAL: {
                uint64_t now = rte_rdtsc_precise();
                double phase_duration;
                
                if (now >= dclognormal_state.phase_end_tsc) {
                    // Switch phase
                    dclognormal_state.in_on_phase = !dclognormal_state.in_on_phase;
                    
                    // Generate new duration for next phase
                    if (dclognormal_state.in_on_phase) {
                        dclognormal_state.current_on_duration = generate_lognormal(
                            traffic_config.params.dclognormal.on_mu,
                            traffic_config.params.dclognormal.on_sigma
                        );
                        phase_duration = dclognormal_state.current_on_duration;
                    } else {
                        dclognormal_state.current_off_duration = generate_lognormal(
                            traffic_config.params.dclognormal.off_mu,
                            traffic_config.params.dclognormal.off_sigma
                        );
                        phase_duration = dclognormal_state.current_off_duration;
                    }
                    
                    dclognormal_state.phase_end_tsc = now + 
                        (uint64_t)(phase_duration * tsc_hz);
                }
                
                if (dclognormal_state.in_on_phase) {
                    target_rate = traffic_config.params.dclognormal.on_rate;
                    tsc_per_packet = (target_rate > 0) ? 
                                    (double)tsc_hz / target_rate : 0;
                    do_rate_limiting = true;
                } else {
                    target_rate = 0;
                    tsc_per_packet = 0;
                    continue;
                }
                break;
            }

            case PATTERN_TLOGN: {
                uint64_t now = rte_rdtsc_precise();
                
                // Check phase transition
                if (now >= tlogn_state.phase_end_tsc) {
                    tlogn_state.in_on_phase = !tlogn_state.in_on_phase;
                    
                    // Generate new phase duration
                    if (tlogn_state.in_on_phase) {
                        tlogn_state.current_on_duration = generate_lognormal(
                            traffic_config.params.tlogn.on_mu,
                            traffic_config.params.tlogn.on_sigma
                        );
                        tlogn_state.phase_end_tsc = now + 
                            (uint64_t)(tlogn_state.current_on_duration * tlogn_state.tsc_hz);
                    } else {
                        tlogn_state.current_off_duration = generate_lognormal(
                            traffic_config.params.tlogn.off_mu,
                            traffic_config.params.tlogn.off_sigma
                        );
                        tlogn_state.phase_end_tsc = now + 
                            (uint64_t)(tlogn_state.current_off_duration * tlogn_state.tsc_hz);
                    }
                    
                    // Reset burst timer when entering ON phase
                    if (tlogn_state.in_on_phase) {
                        tlogn_state.next_burst_tsc = now;
                    }
                }
                
                // Determine how many packets to send in this iteration
                if (tlogn_state.in_on_phase && now >= tlogn_state.next_burst_tsc) {
                    // Time to send a burst
                    burst_to_send = traffic_config.params.tlogn.burst_size;
                    
                    // Schedule next burst using lognormal inter-burst interval
                    double interval = generate_lognormal(
                        traffic_config.params.tlogn.rate_mu,
                        traffic_config.params.tlogn.rate_sigma
                    );
                    tlogn_state.next_burst_tsc = now + 
                        (uint64_t)(interval * tlogn_state.tsc_hz);
                } else {
                    // Either in OFF phase or waiting for next burst → send nothing
                    burst_to_send = 0;
                }
                custom_burst = true;
                do_rate_limiting = false;   // TLOGN uses its own timing
                break;
            }

            case PATTERN_UNIFORM:
            default:
                tsc_per_packet = (target_rate > 0) ? (double)tsc_hz / target_rate : 0;
                do_rate_limiting = true;
                break;
        }

        // If the pattern did NOT set a custom burst, use the global burst size
        if (!custom_burst) {
            burst_to_send = traffic_config.burst_size;
        }

        // Initialize rate state for patterns that need it
        if (do_rate_limiting && target_rate > 0) {
            if (rate_state.tsc_hz == 0) {
                rate_state.tsc_hz = tsc_hz;
                rate_state.packets_per_sec = target_rate;
                rate_state.tsc_per_burst = (uint64_t)((double)tsc_hz / target_rate * traffic_config.burst_size);
                rate_state.next_expected_tsc = rte_rdtsc_precise();
            } else if (rate_state.packets_per_sec != target_rate) {
                rate_state.packets_per_sec = target_rate;
                rate_state.tsc_per_burst = (uint64_t)((double)tsc_hz / target_rate * traffic_config.burst_size);
                rate_state.next_expected_tsc = rte_rdtsc_precise();
            }
        }

        // Calculate burst size
        if (total_packets > 0) {
            burst_to_send = RTE_MIN(burst_to_send, total_packets - stats.tx);
        }
        if (traffic_config.pattern == PATTERN_BURST_PAUSE) {
            burst_to_send = RTE_MIN(burst_to_send, burst_pause_state.burst_remaining);
        }

        // Skip if no packets to send in this iteration
        if (burst_to_send == 0) {
            // in this iteration, there are no packets to send !
            send_empty_queues += 1;
            continue;
        }
 
        // Allocate packets first
        if (rte_pktmbuf_alloc_bulk(mbuf_pool, tx_bufs, burst_to_send) != 0) {
            send_empty_queues += 1;
            continue;
        }
 
        // Get timestamp ONCE before construction

        uint64_t pre_tx_tsc = rte_rdtsc_precise();
 
        // Construct and fill ALL packet fields in ONE LOOP with CONSISTENT pointers

        for (int i = 0; i < burst_to_send; i++) {
            struct rte_mbuf *m = tx_bufs[i];
            char *data = rte_pktmbuf_append(m, PACKET_SIZE);
            
            struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)data;
            struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
            char *payload = (char *)(ip_hdr + 1);
 
            setup_eth_header(eth_hdr, tx_port);
            setup_ip_header(ip_hdr);
            setup_payload(payload);
 
            // Set sequence number and timestamp using the SAME payload pointer
            *(uint64_t*)(payload + SEQ_NUM_OFFSET) = seq_num++;
            *(uint64_t*)(payload + TIMESTAMP_OFFSET) = pre_tx_tsc;
        }
 
        // FIX: Transmit immediately after construction
        uint16_t sent = rte_eth_tx_burst(tx_port, 0, tx_bufs, burst_to_send);
 
        if (stats_enabled) {
            stats.tx += sent;
            stats.interval_tx += sent;
            
            uint64_t wire_bytes = WIRE_SIZE(PACKET_SIZE) * sent;
            total_wire_bytes += wire_bytes;
            interval_wire_bytes += wire_bytes;
        }

        if (traffic_config.pattern == PATTERN_BURST_PAUSE) {
            burst_pause_state.burst_remaining -= sent;
        }

        if (do_rate_limiting && target_rate > 0) {
            uint64_t now = rte_rdtsc_precise();                       // time after transmission
            uint64_t tsc_per_packet = tsc_hz / target_rate;           // integer cycles per packet
            uint64_t expected_next = rate_state.next_expected_tsc + sent * tsc_per_packet;

            // If we are more than a full burst behind, reset schedule to now
            if (now > expected_next + tsc_per_packet * traffic_config.burst_size) {
                rate_state.next_expected_tsc = now;
            }
            // If we are ahead of schedule, wait until the expected time
            else if (now < expected_next) {
                uint64_t wait_until = expected_next;
                while (rte_rdtsc_precise() < wait_until && !force_quit) {
                    rte_pause();
                }
                if (force_quit) break;
                rate_state.next_expected_tsc = expected_next;
            }
            // Otherwise (on time or slightly behind) just update
            else {
                rate_state.next_expected_tsc = expected_next;
            }
        }

        // Free unsent packets
        if (sent < burst_to_send) {
            for (int i = sent; i < burst_to_send; i++) {
                rte_pktmbuf_free(tx_bufs[i]);
            }
        }
    }

    printf("\nI am lcore_send and i got send_empty_queues = %lu\n\n", send_empty_queues);
    return 0;
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------




// --- RECEIVE --- 

static void reset_interval_stats(void) {
#ifdef DEBUG
    rte_spinlock_lock(&stats.interval_lock);
#endif
    memset(stats.interval_histogram, 0, sizeof(stats.interval_histogram));
    stats.interval_max_bin = 0;
    stats.interval_tx = 0;
    stats.interval_rx = 0;
    stats.interval_latency_total = 0;
    stats.interval_min_latency = UINT64_MAX;
    stats.interval_max_latency = 0;
    stats.interval_squared_latency_sum = 0;
    interval_wire_bytes = 0;
    stats.interval_start_tsc = rte_rdtsc_precise();
#ifdef DEBUG
    rte_spinlock_unlock(&stats.interval_lock);
#endif
}


// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



// using Welford algorithm
static int lcore_recv(__rte_unused void *arg) {
    const uint16_t rx_port = PORT_RECEIVE_ON;
    uint64_t tsc_hz = rte_get_tsc_hz();

    struct rte_mbuf *rx_bufs[MAX_BURST_SIZE];

    printf("--- --- --- Starting receive loop on lcore %u --- --- ---\n\n\n", rte_lcore_id());

    // Add port metadata check
    struct rte_eth_link link;
    
    //  rte_eth_link_get(rx_port, &link);
    int link_get_result = rte_eth_link_get(rx_port, &link);
    if (link_get_result < 0) {
        printf("Failed to get link status for port %u: %s\n",
               rx_port, rte_strerror(-link_get_result));
    }
    printf("Port %u - Link status: %s\n", rx_port, link.link_status ? "UP" : "DOWN");
    
    uint64_t huge_latency_warning_us = 1000;

    uint64_t recv_empty_queues = 0;

    while (!force_quit) {
        // Receive burst of packets
        uint16_t nb_rx = rte_eth_rx_burst(rx_port, 0, rx_bufs, traffic_config.burst_size);
        if (nb_rx == 0) {
            recv_empty_queues += 1;
            continue;
        }
        uint64_t recv_ts = rte_rdtsc_precise();

        uint64_t burst_min = UINT64_MAX;
        uint64_t burst_max = 0;
        uint32_t burst_max_bin = 0;
        //uint64_t local_histogram[MAX_BINS] = {0};

        if (stats_enabled) {
            stats.rx += nb_rx;
            stats.interval_rx += nb_rx;
        }

        uint64_t max_latency_this_burst = 0;
        uint64_t max_latency_this_burst_sent_ts = 0;
        uint64_t max_latency_this_burst_recv_ts = 0;
        uint64_t max_latency_this_burst_payload = 0;

        for (int i = 0; i < nb_rx; i++) {
            struct rte_mbuf *m = rx_bufs[i];
            //  // Validate minimum packet size
            //  if (unlikely(m->pkt_len < (sizeof(struct rte_ether_hdr) + 
            //                         sizeof(struct rte_ipv4_hdr) + 8))) {
            //      printf("Dropping undersized packet (%u bytes)\n", m->pkt_len);
            //      rte_pktmbuf_free(m);
            //      continue;
            //  }

            // check packet size minimum :
            if (unlikely(m->pkt_len < MIN_PKT_SIZE)) {
                printf("Dropping invalid packet (%u < %lu)\n", m->pkt_len, MIN_PKT_SIZE);
                rte_pktmbuf_free(m);
                continue;
            }

            char *data = rte_pktmbuf_mtod(m, char *);
            struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(data + sizeof(struct rte_ether_hdr));
            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
            char *payload = (char *)(ip_hdr + 1);

            // Verify magic signature
            if (*(uint32_t*)payload != MAGIC_SIGNATURE) {
                printf("Bad signature: %08x\n", *(uint32_t*)payload);
                rte_pktmbuf_free(m);
                continue;
            }
            
            // Calculate latency
            uint64_t sent_ts = *(uint64_t*)(payload + TIMESTAMP_OFFSET);
            if (recv_ts < sent_ts) {
                // TSC mismatch or timestamp too late; skip this packet
                rte_pktmbuf_free(m);
                continue;
            }
            uint64_t latency = recv_ts - sent_ts;
            
            uint64_t latency_us = (latency * 1000000ULL) / tsc_hz;
            //  //  uint32_t bin = (latency_us >= MAX_BINS) ? MAX_BINS - 1 : (uint32_t)latency_us;
            
            uint64_t half_us = (latency * 2000000ULL) / tsc_hz;
            uint32_t bin = (half_us >= MAX_BINS) ? MAX_BINS - 1 : (uint32_t)half_us;

            if (max_latency_this_burst==0 && latency_us>= huge_latency_warning_us){
                max_latency_this_burst = latency_us;
                max_latency_this_burst_sent_ts = sent_ts;
                max_latency_this_burst_recv_ts = recv_ts;
                max_latency_this_burst_payload = *(uint64_t*)(payload + SEQ_NUM_OFFSET);
            }

            if (stats_enabled) {
                //local_histogram[bin]++;
                stats.histogram[bin]++; 
                if (bin > stats.max_bin) {
                    stats.max_bin = bin;
                }

                if (latency < burst_min) {
                    burst_min = latency;
                }
                if (latency > burst_max) {
                    burst_max = latency;
                }
                if (bin > burst_max_bin) {
                    burst_max_bin = bin;
                }
            
                // Update Welford's algorithm for overall standard deviation
                sample_count++;
                double delta = latency - sample_mean;
                sample_mean += delta / sample_count;
                double delta2 = latency - sample_mean;
                sample_M2 += delta * delta2;
                
                // Update overall stats
                stats.latency_total += latency;
                stats.squared_latency_sum += latency * latency;
                
                if (latency < stats.min_latency || stats.min_latency == 0)
                stats.min_latency = latency;
                if (latency > stats.max_latency)
                    stats.max_latency = latency;
                    
                // Update interval stats
                stats.interval_latency_total += latency;
                stats.interval_squared_latency_sum += latency * latency;
                
                if (latency < stats.interval_min_latency)
                stats.interval_min_latency = latency;
                if (latency > stats.interval_max_latency)
                stats.interval_max_latency = latency;
                
                // Update histogram
                //local_histogram[bin]++;
                if (bin > burst_max_bin) {
                    burst_max_bin = bin;
                }

                #ifdef DEBUG
                rte_spinlock_lock(&stats.interval_lock);
                #endif
                stats.interval_histogram[bin]++;
                if (bin > stats.interval_max_bin) {
                    stats.interval_max_bin = bin;
                }
                #ifdef DEBUG
                rte_spinlock_unlock(&stats.interval_lock);
                #endif
            }
            
            rte_pktmbuf_free(m);
        }
        
        // end of the for-iteration of this batch
        int high_latency_verbose = 0;
        if (high_latency_verbose &&  max_latency_this_burst > huge_latency_warning_us) {
            printf("WARNING: High latency packet: seq=%lu, latency=%lu us, "
                "sent_ts=%lu, recv_ts=%lu\n",
                    max_latency_this_burst_payload,
                    max_latency_this_burst, max_latency_this_burst_sent_ts, max_latency_this_burst_recv_ts);
        }
    }

    printf("\nI am lcore_recv, i got recv_empty_queues = %lu\n\n", recv_empty_queues);
    return 0;
}




// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



static void old_discrete_print_histogram_buckets(void) {
    printf("\n---- Latency Histogram Buckets ----\n");
    printf("Bin (us) | Count\n");
    printf("-----------------\n");
    
    for (uint32_t bin = 0; bin <= stats.max_bin; bin++) {
        if (stats.histogram[bin] > 0) {
            printf("%4u     | %lu\n", bin, stats.histogram[bin]);
        }
    }
}


static void circa_print_histogram_buckets(void) {
    printf("\n---- Latency Histogram Buckets (0.5 µs resolution) ----\n");
    printf("Latency (µs) | Count\n");
    printf("----------------------\n");

    uint64_t above_limit = 0;
    uint32_t display_limit = MAX_BINS; // 1999 * 0.5 = 999.5 µs

    for (uint32_t bin = 0; bin <= stats.max_bin; bin++) {
        if (bin <= display_limit) {
            if (stats.histogram[bin] > 0) {
                printf("%10.2f   | %lu\n", bin * 0.5, stats.histogram[bin]);
            }
        } else {
            above_limit += stats.histogram[bin];
        }
    }
    if (above_limit > 0) {
        printf("  ≥%u   | %lu\n", MAX_BINS/2, above_limit);
    }
}




static void print_histogram_buckets(void) {
    printf("\n---- Latency Histogram Buckets (0.5 µs resolution) ----\n");
    printf("Latency (µs) | Count\n");
    printf("----------------------\n");

    uint64_t total_in_bins = 0;
    double weighted_sum = 0.0;
    uint64_t above_limit = 0;

    for (uint32_t bin = 0; bin <= stats.max_bin; bin++) {
        uint64_t count = stats.histogram[bin];
        if (count > 0) {
            double latency = bin * 0.5;
            printf("%10.2f   | %lu\n", latency, count);
            total_in_bins += count;
            weighted_sum += latency * count;
        }
    }

    // Collect packets above MAX_BINS (which is 5000 bins = 2500 µs)
    for (uint32_t bin = stats.max_bin + 1; bin < MAX_BINS; bin++) {
        above_limit += stats.histogram[bin];
    }

    if (above_limit > 0) {
        printf("  ≥%.2f   | %lu\n", MAX_BINS * 0.5, above_limit);
    }

    printf("\n---- Histogram-based Averages ----\n");
    double hist_avg = 0;
    if (total_in_bins > 0) {
        hist_avg = weighted_sum / total_in_bins;
        printf("Average for packets ≤ %.2f µs: %.4f µs (based on %lu packets)\n",
               MAX_BINS * 0.5, hist_avg, total_in_bins);
    } else {
        printf("No packets in histogram bins.\n");
    }

    // Compare with overall average (requires overall.total_rx and overall.avg_latency)
    if (overall.total_rx > 0) {
        printf("Overall average (all packets): %.4f µs (based on %lu packets)\n",
               overall.avg_latency / (rte_get_tsc_hz() / 1e6), overall.total_rx);
        if (total_in_bins > 0 && overall.total_rx > total_in_bins) {
            printf("The difference (%.2f µs) is due to %lu packets with latency > %.2f µs.\n",
                   (overall.avg_latency / (rte_get_tsc_hz() / 1e6)) - hist_avg,
                   overall.total_rx - total_in_bins,
                   MAX_BINS * 0.5);
        }
    }
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



static double calculate_percentile(uint64_t histogram[], uint32_t max_bin, uint64_t total, double percentile) {
    if (total == 0) return 0.0;

    uint64_t desired = (uint64_t)ceil(percentile * total);
    uint64_t accumulated = 0;

    for (uint32_t bin = 0; bin <= max_bin; bin++) {
        accumulated += histogram[bin];
        if (accumulated >= desired) {
            //  return (double)bin; // Return the bin index as microseconds
            return (double)bin * 0.5;
        }
    }
    
    return max_bin > 0 ? (double)max_bin : 0.0;
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


static void print_interval_stats(void) {
    uint64_t tsc_hz = rte_get_tsc_hz();
    uint64_t interval_packets = stats.interval_rx;
    
    // Get actual interval duration in seconds
    uint64_t now_tsc = rte_rdtsc_precise();
    double interval_seconds = (double)(now_tsc - stats.interval_start_tsc) / tsc_hz;
    
    if (interval_seconds < 0.000001) {
        interval_seconds = 0.000001;  // Avoid division by zero
    }
    
    printf("\n---- Interval Statistics (Duration: %.6f sec) ----\n", interval_seconds);
    printf("TX packets: %lu\n", stats.interval_tx);
    printf("RX packets: %lu\n", stats.interval_rx);
    
    if (stats.interval_tx > 0) {
        int64_t loss = (int64_t)stats.interval_tx - (int64_t)stats.interval_rx;
        if (loss < 0) loss = 0;
        printf("Loss: %.4f%%\n", 100.0 * loss / (double)stats.interval_tx);
    } else {
        printf("Loss: N/A\n");
    }

    
    if (stats.interval_rx > 0 && interval_seconds > 0) {
        
        uint64_t wire_size = WIRE_SIZE(PACKET_SIZE);  // PACKET_SIZE + 24
        double actual_pps = (double)stats.interval_rx / interval_seconds;
        double actual_bps = actual_pps * wire_size * 8.0;
        
        // Calculate theoretical max for 10 Gbps link
        double max_pps = 10.0e9 / (wire_size * 8.0);
        double line_rate_utilization = (actual_pps / max_pps) * 100.0;
        
        printf("Actual Throughput:\n");
        printf("  Packets/sec: %.4f (%.4f Mpps)\n", actual_pps, actual_pps / 1e6);
        printf("  Wire rate: %.4f Gbps (%.4f%% of line rate)\n", 
               actual_bps / 1e9, line_rate_utilization);
        
        // Calculate payload efficiency
        double payload_bps = actual_pps * PACKET_SIZE * 8.0;
        printf("  Payload rate: %.4f Gbps (%.4f%% efficiency)\n",
               payload_bps / 1e9, (payload_bps / actual_bps) * 100);
    } else {
        printf("Throughput: No packets received\n");
    }
    
    if (interval_packets > 0) {
        double p99 = calculate_percentile(stats.interval_histogram, 
                                          stats.interval_max_bin, 
                                          interval_packets, 0.99);
        double p95 = calculate_percentile(stats.interval_histogram, 
                                          stats.interval_max_bin, 
                                          interval_packets, 0.95);
        
        double tsc_per_us = tsc_hz / 1e6;
        double avg_latency = (double)stats.interval_latency_total / interval_packets;
        double stddev = 0.0;
        if (interval_packets > 1) {
            double variance = ((double)stats.interval_squared_latency_sum / interval_packets) - 
                                (avg_latency * avg_latency);
            if (variance > 0) {
                stddev = sqrt(variance);
            }
        }
        printf("Latency Statistics (us):\n");
        printf("  95th percentile: %.4f us\n", p95);
        printf("  99th percentile: %.4f us\n", p99);
        printf("  Min: %.4f us\n", (double)stats.interval_min_latency / tsc_per_us);
        printf("  Max: %.4f us\n", (double)stats.interval_max_latency / tsc_per_us);
        printf("  Avg: %.4f us\n", avg_latency / (tsc_hz / 1e6));        
        printf("  StdDev: %.4f us\n", stddev / (tsc_hz / 1e6));
    }
    
    // Reset interval stats
    reset_interval_stats();
}








// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



static void calculate_overall_stats(void) {
    overall.total_tx = stats.tx;
    overall.total_rx = stats.rx;
    overall.min_latency = stats.min_latency;
    overall.max_latency = stats.max_latency;
    
    if (stats.rx > 0) {
        overall.avg_latency = (double)stats.latency_total / stats.rx;
        overall.p95 = calculate_percentile(stats.histogram, stats.max_bin, overall.total_rx, 0.95);
        overall.p99 = calculate_percentile(stats.histogram, stats.max_bin, overall.total_rx, 0.99);
        // Use the results from Welford's algorithm
        if (sample_count > 1) {
            overall.stddev_latency = sqrt(sample_M2 / (sample_count - 1));
        } else {
            overall.stddev_latency = 0;
        }
    }
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



static void print_overall_stats(void) {
    uint64_t tsc_hz = rte_get_tsc_hz();
    
    printf("===============================\n");
    print_histogram_buckets();
    printf("===============================\n");
    printf("\n===============================\n");
    printf("       OVERALL STATISTICS       \n");
    printf("===============================\n");
    
    printf("Total TX packets: %lu\n", overall.total_tx);
    printf("Total RX packets: %lu\n", overall.total_rx);
    
    if (overall.total_tx > 0) {
        int64_t loss = (int64_t)overall.total_tx - (int64_t)overall.total_rx;
        if (loss < 0) loss = 0;
        printf("Overall Loss: %.4f%%\n", 
               100.0 * loss / (double)overall.total_tx);
    } else {
        printf("Overall Loss: N/A\n");
    }

    // Calculate correct overall duration
    uint64_t end_tsc = rte_rdtsc_precise();
    double total_duration_sec = (double)(end_tsc - global_start_tsc) / tsc_hz;  // Total runtime
    
    if (total_duration_sec > 0 && overall.total_rx > 0) {
        uint64_t wire_size = WIRE_SIZE(PACKET_SIZE);  // PACKET_SIZE + 24
        double avg_pps = (double)overall.total_rx / total_duration_sec;
        double avg_bps = avg_pps * wire_size * 8.0;
        
        printf("Average wire rate: %.4f Gbps (%.4f Mpps)\n", 
               avg_bps / 1e9, avg_pps / 1e6);
        
        // Theoretical maximum calculation
        double max_pps = 10.0e9 / (wire_size * 8.0);
        printf("Theoretical max (for a 10Gbps): %.4f Mpps (for %lu-byte packets with %lu-byte wire size)\n",
               max_pps / 1e6, PACKET_SIZE, wire_size);
        printf("Achieved: %.1f%% of line rate (for a 10Gbps)\n", (avg_pps / max_pps) * 100);
    }
    
    if (overall.total_rx > 0) {
        double tsc_per_us = tsc_hz / 1e6;
        printf("Overall 95th percentile latency: %.4f us\n", overall.p95);
        printf("Overall 99th percentile latency: %.4f us\n", overall.p99);
        printf("Overall Min latency: %.4f us\n", 
               (double)overall.min_latency / tsc_per_us);
        printf("Overall Max latency: %.4f us\n", 
               (double)overall.max_latency / tsc_per_us);
        printf("Overall Avg latency: %.4f us\n", 
               overall.avg_latency / tsc_per_us);
        printf("Overall StdDev latency: %.4f us\n", 
               overall.stddev_latency / tsc_per_us);
    } else {
        printf("No packets received overall\n");
    }
    printf("===============================\n");
    printf("Test duration: %.4f seconds\n", total_duration_sec);
    printf("===============================\n");
}

                                                                      


// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



static void signal_handler(int signum) { 
    if (!force_quit) {
        printf("\nSignal %d received, waiting for threads to exit...\n", signum);
        force_quit = true;
    }
}




// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------




static int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = {
        .rxmode = { 
            .max_lro_pkt_size = RTE_ETHER_MAX_LEN , 
            .offloads = RTE_ETH_RX_OFFLOAD_VLAN_STRIP  // Ensure CRC stripping 
        },
        .txmode = { .offloads = RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE },
    };
    printf("Called port_init on port %d\n", port);

    // Force promiscuous mode
    port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    int ret = rte_eth_dev_configure(port, 1, 1, &port_conf);
    if (ret != 0) return ret;

    struct rte_ether_addr mac_addr;
    rte_eth_macaddr_get(port, &mac_addr);
    printf("Port %u MAC: %02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8":%02"PRIx8"\n",
           port,
           mac_addr.addr_bytes[0], 
           mac_addr.addr_bytes[1],
           mac_addr.addr_bytes[2], 
           mac_addr.addr_bytes[3],
           mac_addr.addr_bytes[4], 
           mac_addr.addr_bytes[5]);

    ret = rte_eth_rx_queue_setup(port, 0, 2048, 
           rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (ret < 0) return ret;

    ret = rte_eth_tx_queue_setup(port, 0, 1024, 
            rte_eth_dev_socket_id(port), NULL);
    if (ret < 0) return ret;

    ret = rte_eth_dev_start(port);
    if (ret < 0) return ret;

    ret = rte_eth_promiscuous_enable(port);
    if (ret != 0) {
        printf("Failed to enable promiscuous mode on port %d: %s\n",
               port, rte_strerror(-ret));
    }

    int promisc = rte_eth_promiscuous_get(port);
    if (promisc < 0) {
        printf("Error getting promiscuous status: %s\n", rte_strerror(-promisc));
    } else {
        printf("Port %d promiscuous mode: %s\n", 
               port, promisc ? "ON" : "OFF");
    }

    printf("Port %u Configuration:\n", port);
    printf("- Promiscuous mode: %s\n", 
        (port_conf.rxmode.mq_mode == RTE_ETH_MQ_RX_RSS) ? "RSS" : "Disabled");
    struct rte_eth_link link;
    
    //  rte_eth_link_get_nowait(port, &link);
    int link_status = rte_eth_link_get_nowait(port, &link);
    if (link_status < 0) {
        printf("Failed to get link status for port %u: %s\n",
               port, rte_strerror(-link_status));
    }

    printf("- Link Status: %s\n", link.link_status ? "UP" : "DOWN");
    printf("- Link Speed: %u Mbps\n", link.link_speed);

    return 0;
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



static void usage(const char *prgname) {
    printf( "Usage: %s [EAL options] -- [app options]\n"
            "Options:\n"
            "  -r RATE    Target packet rate (packets/sec)\n"
            "  -n NUM     Total packets to send (0=unlimited)\n"
            "  -s SIZE    Packet size (bytes)\n"
            "  -p PATTERN Traffic pattern (uniform|burst|sawtooth|poisson|random|dc|onoff)\n"
            "  -b BURST   Burst size (for burst pattern)\n"
            "  -t TIME    Pause time (seconds) (burst pattern)\n"
            "  -S START   Start rate (sawtooth)\n"
            "  -E END     End rate (sawtooth)\n"
            "  -d DUR     Step duration (sawtooth)\n"
            "  -c COUNT   Number of steps (sawtooth)\n"
            "  -P LAMBDA  Avg packets/sec (poisson)\n"
            "  -k BURST   Burst size (poisson)\n"
            "  -m MIN     Min rate (random)\n"
            "  -M MAX     Max rate (random)\n"
            "  -i INTERVAL Change interval (random)\n"
            "  -D BASE    Base rate (dc)\n"
            "  -F BURST_R Burst rate (dc)\n"
            "  -G DUR    Burst duration (dc)\n"
            "  -I INTERVAL Burst check interval (dc)\n"
            "  -B SIZE    Burst size (1-%d, default %d)\n"
            "  -U TON     ON duration (seconds)\n"
            "  -N TOFF    OFF duration (seconds)\n"
            "  -R RATE    Rate during ON phase\n"
            "  -L RATE ON_MU ON_SIGMA OFF_MU OFF_SIGMA  DC ON/OFF Lognormal\n"
            "  -w warmup_us  number of us to wait before collecting stats (default 10000us)\n"
            "  -h         Show help\n"
            "\nExamples:\n"
            "  Uniform:     -p uniform -r 1000000\n"
            "  Burst:       -p burst -b 1000 -t 0.5\n"
            "  DClognormal: -p lognormal -L 1000000 0.061385 0.1 0.081732 0.1\n"
            "  DClognormal: -p lognormal -L 1000000 0.061385 0.1 0.081732 0.1\n"
"               (Note: mu,sigma are for ln(duration_in_seconds).\n"
"                Mean duration = exp(mu + sigma^2/2) seconds)\n"
            "  tlogn:       -B 32 -s 256 -p tlogn -T 0.061385 0.1 0.081732 0.1 0.011119 0.1\n"
            "  tlogn:       -B 32 -s 256 -p tlogn -T 0.061385 0.1 0.081732 0.1 0.011119 0.1\n"
"               (Note: mu,sigma are for ln(duration_in_seconds).\n"
"                Mean duration = exp(mu + sigma^2/2) seconds)\n"
            "  Sawtooth:    -p sawtooth -S 1e6 -E 10e6 -d 1 -c 5\n"
            "  \n multipleExpLogn: -p multipleExpLogn num_users exp_rate mu sigma min_bytes max_bytes [bitrate]\n"
            "  FTP (3GPP defaults): -p multipleExpLogn -- 350  0.006  14.45  0.35 100 5000000 25000000000\n"
            "  \n web:             -p web num_users main_mu main_sigma main_min main_max "
                    "emb_mu emb_sigma emb_min emb_max pareto_alpha pareto_k pareto_m "
                    "parsing_lambda reading_lambda [target_bps]\n"
            "  Web (3GPP defaults): -p web -- 1000000 8.37 1.37 100 2000000 6.17 2.36 50 2000000 1.1 2.0 55.0 7.69 0.033 10000000000\n\n"
            "  Gaming (3GPP defaults): -p gaming num_users direction(ul/dl) [target_bps]\n"
            "\n",
            prgname, MAX_BURST_SIZE, DEFAULT_BURST_SIZE);
}


//  sudo ./latency_test -l 2,4,6 -- -B 128 -s 300 -p multipleExpLogn -- 350  0.006  14.45  0.35 100 5000000 25000000000
//  sudo ./latency_test -l 2,4,6 -- -B 32 -s 256 -p web 1000000 8.37 1.37 100 2000000 6.17 2.36 50 2000000 1.1 2.0 55.0 7.69 0.033


// sudo ./latency_test -l 0,2,4   -- -p uniform -r 8000000 -s 128 
// sudo ./latency_test -l 0,2,4   -- -B 32 -s 256 -p tlogn -T 0.061385 0.1 0.081732 0.1 0.011119 0.1
// sudo ./latency_test -l 0,2,4   -- -B 32 -s 256 -p tlogn -T 0.000061385 0.1 0.000081732 0.1 0.000011119 0.1

// sudo ./latency_test -l 0,2,4 -- -B 32 -s 256 -p tlogn -T -9.9 0.1 -3.0 0.1 -9.9 0.1



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

static int parse_app_args(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "r:n:s:p:b:t:S:E:d:c:B:P:k:m:M:i:D:F:G:I:U:N:R:L:T:h")) != -1) {
        switch (opt) {
            case 'r': target_rate = strtoull(optarg, NULL, 10); break;
            case 'n': total_packets = strtoull(optarg, NULL, 10); break;
            case 's': PACKET_SIZE = strtoull(optarg, NULL, 10); break;
            case 'p':
                if (strcmp(optarg, "burst") == 0)
                    traffic_config.pattern = PATTERN_BURST_PAUSE;
                else if (strcmp(optarg, "sawtooth") == 0)
                    traffic_config.pattern = PATTERN_SAWTOOTH;
                else if (strcmp(optarg, "poisson") == 0)
                    traffic_config.pattern = PATTERN_POISSON;
                else if (strcmp(optarg, "random") == 0)
                    traffic_config.pattern = PATTERN_RANDOM_RATE;
                else if (strcmp(optarg, "dc") == 0)
                    traffic_config.pattern = PATTERN_DATACENTER;
                else if (strcmp(optarg, "onoff") == 0)
                    traffic_config.pattern = PATTERN_ON_OFF;
                else if (strcmp(optarg, "lognormal") == 0)
                    traffic_config.pattern = PATTERN_DC_ON_OFF_LOGNORMAL;
                else if (strcmp(optarg, "tlogn") == 0)
                    traffic_config.pattern = PATTERN_TLOGN;
                else if (strcmp(optarg, "multipleExpLogn") == 0)
                    traffic_config.pattern = PATTERN_MULTI_EXP_LOGN;
                else if (strcmp(optarg, "web") == 0)
                    traffic_config.pattern = PATTERN_WEB;
                else if (strcmp(optarg, "gaming") == 0)
                    traffic_config.pattern = PATTERN_GAMING;
                // If none match, it remains PATTERN_UNIFORM which is 0
                break;
            case 'b': traffic_config.params.bp.burst_size = strtoull(optarg, NULL, 10); break;
            case 't': traffic_config.params.bp.pause_sec = atof(optarg); break;
            case 'S': traffic_config.params.st.start_rate = strtoull(optarg, NULL, 10); break;
            case 'E': traffic_config.params.st.end_rate = strtoull(optarg, NULL, 10); break;
            case 'd': traffic_config.params.st.step_duration = atof(optarg); break;
            case 'c': traffic_config.params.st.num_steps = atoi(optarg); break;
            case 'B': 
                traffic_config.burst_size = (uint16_t)atoi(optarg);
                if (traffic_config.burst_size < 1) {
                    traffic_config.burst_size = 1;
                } else if (traffic_config.burst_size > MAX_BURST_SIZE) {
                    traffic_config.burst_size = MAX_BURST_SIZE;
                }
                break;
            case 'P': 
                traffic_config.params.poisson.lambda = atof(optarg);
                break;
            case 'k': 
                traffic_config.params.poisson.burst_size = atoi(optarg);
                break;
            case 'm': 
                traffic_config.params.random_rate.min_rate = strtoull(optarg, NULL, 10);
                break;
            case 'M': 
                traffic_config.params.random_rate.max_rate = strtoull(optarg, NULL, 10);
                break;
            case 'i': 
                traffic_config.params.random_rate.change_interval = atof(optarg);
                break;
            case 'D': 
                traffic_config.params.dc.base_rate = strtoull(optarg, NULL, 10);
                break;
            case 'F': 
                traffic_config.params.dc.burst_rate = strtoull(optarg, NULL, 10);
                break;
            case 'G': 
                traffic_config.params.dc.burst_duration = atof(optarg);
                break;
            case 'I': 
                traffic_config.params.dc.burst_interval = atof(optarg);
                break;
                case 'U':  // TON duration
                traffic_config.params.onoff.ton = atof(optarg);
                if (traffic_config.params.onoff.toff == 0)
                    traffic_config.params.onoff.toff = traffic_config.params.onoff.ton;
                break;
            case 'N':  // TOFF duration
                traffic_config.params.onoff.toff = atof(optarg);
                if (traffic_config.params.onoff.ton == 0)
                    traffic_config.params.onoff.ton = traffic_config.params.onoff.toff;
                break;
            case 'R':  // Rate during ON phase
                traffic_config.params.onoff.rate = strtoull(optarg, NULL, 10);
                break;
            case 'L':  // DC lognormal pattern
                traffic_config.pattern = PATTERN_DC_ON_OFF_LOGNORMAL;
                    
                // Parse the first argument (rate) directly from optarg
                traffic_config.params.dclognormal.on_rate = strtoull(optarg, NULL, 10);
                
                // Check if we have enough remaining arguments
                if (optind + 3 >= argc) {
                    fprintf(stderr, "Error: -L requires 5 arguments (including the rate)\n");
                    return -1;
                }
                // Parse the remaining 4 parameters
                traffic_config.params.dclognormal.on_mu = atof(argv[optind++]);
                traffic_config.params.dclognormal.on_sigma = atof(argv[optind++]);
                traffic_config.params.dclognormal.off_mu = atof(argv[optind++]);
                traffic_config.params.dclognormal.off_sigma = atof(argv[optind++]);
                break;

            case 'T':  // TLOGN pattern
                traffic_config.pattern = PATTERN_TLOGN;
                // Get the first parameter from optarg
                traffic_config.params.tlogn.on_mu = atof(optarg);
                // Check if we have enough remaining arguments
                if (optind + 4 >= argc) {
                    fprintf(stderr, "Error: -T requires 5 additional arguments\n");
                    return -1;
                }
                // Parse the remaining 5 parameters
                traffic_config.params.tlogn.on_sigma = atof(argv[optind++]);
                traffic_config.params.tlogn.off_mu = atof(argv[optind++]);
                traffic_config.params.tlogn.off_sigma = atof(argv[optind++]);
                traffic_config.params.tlogn.rate_mu = atof(argv[optind++]);
                traffic_config.params.tlogn.rate_sigma = atof(argv[optind++]);
                // Use burst size from -B option
                traffic_config.params.tlogn.burst_size = traffic_config.burst_size;
                if (traffic_config.params.tlogn.burst_size == 0) {
                    traffic_config.params.tlogn.burst_size = DEFAULT_BURST_SIZE;
                }
                break;
            case 'w':
                warmup_us = strtoll(optarg, NULL, 10);
                break;
            case 'h': usage(argv[0]); exit(0);
            default: usage(argv[0]); return -1;
        }
    }

    if (traffic_config.pattern == PATTERN_ON_OFF) {
        // Set default time values if not provided
        if (traffic_config.params.onoff.ton == 0 && traffic_config.params.onoff.toff == 0) {
            traffic_config.params.onoff.ton = 0.5;
            traffic_config.params.onoff.toff = 0.5;
        } else if (traffic_config.params.onoff.ton == 0) {
            traffic_config.params.onoff.ton = traffic_config.params.onoff.toff;
        } else if (traffic_config.params.onoff.toff == 0) {
            traffic_config.params.onoff.toff = traffic_config.params.onoff.ton;
        }
        
        // Verify required parameters
        if (traffic_config.params.onoff.rate == 0) {
            printf("ON/OFF pattern using maximum rate (no rate limiting)\n");
        }
    }

    if (traffic_config.pattern == PATTERN_MULTI_EXP_LOGN) {
        // Need at least 6 more arguments: num_users exp_rate mu sigma min_bytes max_bytes
        // e.g. : -p multipleExpLogn 100 10.0 6.0 1.0 100 3000 8000000000
        if (optind + 6 > argc) {
            fprintf(stderr, "Error: multipleExpLogn requires 6 arguments: "
                    "num_users exp_rate mu sigma min_bytes max_bytes [bitrate]\n");
            return -1;
        }
        traffic_config.params.multi_exp_logn.num_users = atoi(argv[optind++]);
        traffic_config.params.multi_exp_logn.exp_rate = atof(argv[optind++]);
        traffic_config.params.multi_exp_logn.logn_mu = atof(argv[optind++]);
        traffic_config.params.multi_exp_logn.logn_sigma = atof(argv[optind++]);
        traffic_config.params.multi_exp_logn.min_bytes = atoi(argv[optind++]);
        traffic_config.params.multi_exp_logn.max_bytes = atoi(argv[optind++]);
        // Optional bitrate (default 8 Gbps)
        if (optind < argc && argv[optind][0] != '-') {   // assume it's a number
            traffic_config.params.multi_exp_logn.target_bps = strtoull(argv[optind++], NULL, 10);
        } else {
            traffic_config.params.multi_exp_logn.target_bps = DEFAULT_TARGET_BPS; 
        }
    }


    if (traffic_config.pattern == PATTERN_WEB) {
        // Required arguments: num_users, main_mu, main_sigma, main_min, main_max,
        // emb_mu, emb_sigma, emb_min, emb_max, pareto_alpha, pareto_k, pareto_m,
        // parsing_lambda, reading_lambda [, target_bps]
        int required = 14; // 1 + 4 + 4 + 3 + 2
        if (optind + required > argc) {
            fprintf(stderr, "Error: web requires %d arguments: "
                    "num_users main_mu main_sigma main_min main_max "
                    "emb_mu emb_sigma emb_min emb_max pareto_alpha pareto_k pareto_m "
                    "parsing_lambda reading_lambda [target_bps]\n", required);
            return -1;
        }
        traffic_config.params.web.num_users = atoi(argv[optind++]);
        traffic_config.params.web.main_mu = atof(argv[optind++]);
        traffic_config.params.web.main_sigma = atof(argv[optind++]);
        traffic_config.params.web.main_min = atoi(argv[optind++]);
        traffic_config.params.web.main_max = atoi(argv[optind++]);
        traffic_config.params.web.emb_mu = atof(argv[optind++]);
        traffic_config.params.web.emb_sigma = atof(argv[optind++]);
        traffic_config.params.web.emb_min = atoi(argv[optind++]);
        traffic_config.params.web.emb_max = atoi(argv[optind++]);
        traffic_config.params.web.pareto_alpha = atof(argv[optind++]);
        traffic_config.params.web.pareto_k = atof(argv[optind++]);
        traffic_config.params.web.pareto_m = atof(argv[optind++]);
        traffic_config.params.web.parsing_lambda = atof(argv[optind++]);
        traffic_config.params.web.reading_lambda = atof(argv[optind++]);
        if (optind < argc && argv[optind][0] != '-') {
            traffic_config.params.web.target_bps = strtoull(argv[optind++], NULL, 10);
        } else {
            traffic_config.params.web.target_bps = DEFAULT_TARGET_BPS; 
        }
    }

    if (traffic_config.pattern == PATTERN_GAMING) {
        // Minimal required: num_users direction
        if (optind + 2 > argc) {
            fprintf(stderr, "Error: gaming requires at least num_users and direction\n");
            return -1;
        }
        traffic_config.params.gaming.num_users = atoi(argv[optind++]);
        char *dir_str = argv[optind++];
        if (strcasecmp(dir_str, "ul") == 0)
            traffic_config.params.gaming.direction = 0;
        else if (strcasecmp(dir_str, "dl") == 0)
            traffic_config.params.gaming.direction = 1;
        else {
            fprintf(stderr, "Error: direction must be 'ul' or 'dl'\n");
            return -1;
        }

        // Optional: if more arguments exist, parse all distribution parameters.
        // You may decide to make them mandatory or provide defaults.
        // Example for full parameter list:
        if (optind + 12 <= argc) {   // 12 more parameters (see struct above)
            traffic_config.params.gaming.ul_initial_a   = atof(argv[optind++]);
            traffic_config.params.gaming.ul_initial_b   = atof(argv[optind++]);
            traffic_config.params.gaming.ul_arrival_type = atoi(argv[optind++]); // 0/1
            traffic_config.params.gaming.ul_arrival_a   = atof(argv[optind++]);
            traffic_config.params.gaming.ul_arrival_b   = atof(argv[optind++]);
            traffic_config.params.gaming.ul_size_a      = atof(argv[optind++]);
            traffic_config.params.gaming.ul_size_b      = atof(argv[optind++]);
            traffic_config.params.gaming.dl_initial_a   = atof(argv[optind++]);
            traffic_config.params.gaming.dl_initial_b   = atof(argv[optind++]);
            traffic_config.params.gaming.dl_arrival_a   = atof(argv[optind++]);
            traffic_config.params.gaming.dl_arrival_b   = atof(argv[optind++]);
            traffic_config.params.gaming.dl_size_a      = atof(argv[optind++]);
            traffic_config.params.gaming.dl_size_b      = atof(argv[optind++]);
        } else {
            // Set defaults (e.g., the first table values)
            traffic_config.params.gaming.ul_initial_a   = 0.0;
            traffic_config.params.gaming.ul_initial_b   = 0.04;
            traffic_config.params.gaming.ul_arrival_type = 0; // deterministic
            traffic_config.params.gaming.ul_arrival_a   = 0.04; // (not used)
            traffic_config.params.gaming.ul_arrival_b   = 0.0;
            traffic_config.params.gaming.ul_size_a      = 45.0;
            traffic_config.params.gaming.ul_size_b      = 5.7;
            traffic_config.params.gaming.dl_initial_a   = 0.0;
            traffic_config.params.gaming.dl_initial_b   = 0.04;
            traffic_config.params.gaming.dl_arrival_a   = 0.055;
            traffic_config.params.gaming.dl_arrival_b   = 0.006;
            traffic_config.params.gaming.dl_size_a      = 120.0;
            traffic_config.params.gaming.dl_size_b      = 36.0;
        }

        // Optional target bitrate
        if (optind < argc && argv[optind][0] != '-')
            traffic_config.params.gaming.target_bps = strtoull(argv[optind++], NULL, 10);
        else
            traffic_config.params.gaming.target_bps = 0;
    }

    return 0;
}


// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


// Add this function to check TSC sync
static void check_tsc_sync(void) {
    uint64_t tsc_hz = rte_get_tsc_hz();
    uint64_t tsc1, tsc2, tsc3;
    
    // Check if TSC increments properly
    tsc1 = rte_get_tsc_cycles();
    rte_delay_us(100);  // 100µs delay
    tsc2 = rte_get_tsc_cycles();
    uint64_t elapsed = tsc2 - tsc1;
    double expected = 0.0001 * tsc_hz;  // 100µs in cycles
    
    printf("TSC Check: tsc1=%lu, tsc2=%lu, elapsed=%lu (expected ~%.0f)\n",
        tsc1, tsc2, elapsed, expected);
    
    // Check TSC consistency across a difference time skip
    if (elapsed < expected * 0.9 || elapsed > expected * 1.1) {
        printf("WARNING: TSC may not be stable! Drift: %.1f%%\n",
            100.0 * (elapsed - expected) / expected);
    }
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------




// Check TSC on different cores
static void check_core_tsc_difference(void) {
    unsigned int lcore_id = rte_lcore_id();
    uint64_t tsc = rte_get_tsc_cycles();
    printf("Core %u TSC: %lu\n", lcore_id, tsc);
}



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------



int main(int argc, char **argv) {
    printf("argc : %d\n", argc);
    for (int i = 0; i<argc; i++){
        printf("%s\n", argv[i]);
    }
    printf("printed the inputs !!!\n");

#ifdef DEBUG
    printf("Debug mode is ON\n");
#endif

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        printf("Sadly, EAL failed, rip.\n\n");
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    }

    printf("EAL completed !!! \n\n");

    // Set stdout to unbuffered to ensure immediate output to log file
    setbuf(stdout, NULL);

    // check : sudo perf trace -p <pid> -e sched:sched_switch

    // initial time-sync checks
    check_core_tsc_difference();
    check_tsc_sync();


    global_start_tsc = rte_rdtsc_precise();

    // set default burst size to 32, later will parse args to update it if needed
    traffic_config.burst_size = DEFAULT_BURST_SIZE;

    // Skip the EAL args
    argc -= ret;
    argv += ret;

    // Parse application args
    ret = parse_app_args(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Invalid application arguments\n");


    //printf("executing traffic pattern : %d", traffic_config.pattern);
    // Print which pattern is being used
    printf("Selected traffic pattern: ");
    switch (traffic_config.pattern) {
        case PATTERN_UNIFORM:               printf("UNIFORM\n"); break;
        case PATTERN_BURST_PAUSE:           printf("BURST_PAUSE\n"); break;
        case PATTERN_SAWTOOTH:              printf("SAWTOOTH\n"); break;
        case PATTERN_POISSON:               printf("POISSON\n"); break;
        case PATTERN_RANDOM_RATE:           printf("RANDOM_RATE\n"); break;
        case PATTERN_DATACENTER:            printf("DATACENTER\n"); break;
        case PATTERN_ON_OFF:                printf("ON-OFF\n"); break;
        case PATTERN_DC_ON_OFF_LOGNORMAL:   printf("PATTERN_DC_ON_OFF_LOGNORMAL\n"); break;
        case PATTERN_TLOGN:                 printf("PATTERN_TLOGN\n"); break;
        case PATTERN_MULTI_EXP_LOGN:        printf("PATTERN_MULTI_EXP_LOGN\n"); break;
        case PATTERN_WEB:                   printf("PATTERN_WEB\n"); break;
        case PATTERN_GAMING:                printf("GAMING\n"); break;
        default:                            printf("UNKNOWN\n"); break;
    }


    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGUSR1, signal_handler);

    printf("---- using PACKET_SIZE : %ld \n", PACKET_SIZE);
    printf("-> trying to generate : PACKET_SIZE*8*target_rate : %ld bits per second \n", PACKET_SIZE*8*target_rate);

    // Print physical limits
    printf("\n=== Physical Limits Analysis ===\n");
    uint64_t wire_size = WIRE_SIZE(PACKET_SIZE);
    double max_pps_10g = (10.0e9) / (wire_size * 8.0);
    //double max_pps_25g = (25.0e9) / (wire_size * 8.0);
    double max_pps_40g = (40.0e9) / (wire_size * 8.0);
    
    double target_Mbps = 8*wire_size*target_rate/1e6;

    printf("For %lu-byte packets:\n", PACKET_SIZE);
    printf("  Wire size: %lu bytes (including overhead)\n", wire_size);
    printf("  Theoretical maximum rates:\n");
    printf("    10 Gbps: %.4f Mpps\n", max_pps_10g / 1e6);
    //printf("    25 Gbps: .4f Mpps\n", max_pps_25g / 1e6);
    printf("    40 Gbps: %.4f Mpps\n", max_pps_40g / 1e6);
    printf("  Your requested wire rate is : %.4f Mpps\n", target_rate/1e6);
    printf("  --- which would be %.4f Gbps (%.4f Mbps) \n", target_Mbps/1e3, target_Mbps);


    if (target_rate > 0 && target_rate > max_pps_10g) {
        printf("\nWARNING: Target rate exceeds 10 Gbps link capacity!\n");
    }

    if (target_rate > 0 && target_rate > max_pps_40g) {
        printf("\nWARNING: Target rate exceeds 40 Gbps link capacity!\n");
    }


#ifdef DEBUG
    rte_spinlock_init(&stats.interval_lock);
#endif

    int socket_id = rte_socket_id();
    printf("Socket ID: %d\n", socket_id);
    printf("Data room size: %u\n", RTE_MBUF_DEFAULT_BUF_SIZE);
    printf("Cache size: 0, priv_size: 0, nb_mbufs: 8192\n");
    

    
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", MBUF_POOL_SIZE, 
        MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    //  //  rte_pktmbuf_pool_create(s, nb_mbuf,
    //  //              MEMPOOL_CACHE_SIZE, 0,
    //  //              RTE_MBUF_DEFAULT_BUF_SIZE,
    //  //              socketid);
    
    if (!mbuf_pool) {
        RTE_LOG(ERR, USER1, "Cannot create mbuf pool: %s\n", rte_strerror(rte_errno));
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
        return -1;
    }

    // Initialize statistics
    stats.min_latency = UINT64_MAX;
    stats.max_latency = 0;
    reset_interval_stats();

    for (uint16_t port = 0; port < NUM_PORTS; port++) {
        if (port_init(port, mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Port %u init failed\n", port);
    }

    for (uint16_t port = 0; port < NUM_PORTS; port++) {
       struct rte_ether_addr mac;
       rte_eth_macaddr_get(port, &mac);
       printf("Port %d configured MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           port,
           mac.addr_bytes[0], 
           mac.addr_bytes[1],
           mac.addr_bytes[2], 
           mac.addr_bytes[3],
           mac.addr_bytes[4], 
           mac.addr_bytes[5]);
    }
    
    // Additional sawtooth validation
    if (traffic_config.pattern == PATTERN_SAWTOOTH) {
        if (traffic_config.params.st.num_steps < 2) {
            rte_exit(EXIT_FAILURE, "Sawtooth needs at least 2 steps\n");
        }
        if (traffic_config.params.st.end_rate <= traffic_config.params.st.start_rate) {
            rte_exit(EXIT_FAILURE, "End rate must be > start rate\n");
        }
    }

    // Launch cores and statistics loop
    // Get available worker cores

    /*
    uint32_t trg_core_send = 4;
    uint32_t trg_core_recv = 6;
    printf("Pinning sender on lcore %d ; and receiver on lcore %d .\n", trg_core_send, trg_core_recv);

    int send_res = rte_eal_remote_launch(lcore_send, NULL, trg_core_send);
    int recv_res = rte_eal_remote_launch(lcore_recv, NULL, trg_core_recv);
    
    printf("pinnati con successo(==0) : send %d, recv %d \n ", send_res, recv_res);
    */

    //exit(1);
    unsigned int lcore_id;
    unsigned int worker_count = 0;

    
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        worker_count++;
        if (worker_count == 1) {
            rte_eal_remote_launch(lcore_send, NULL, lcore_id);
            printf("--- --- --- main launched sender on lcore %d --- --- ---\n", lcore_id);
        } else if (worker_count == 2) {
            rte_eal_remote_launch(lcore_recv, NULL, lcore_id);
            printf("--- --- --- main launched receiver on lcore %d --- --- ---\n", lcore_id);
        }
    }


    // if warmup period was not specified, default to DEFAULT_WARMUP_US duration !
    // non-initialized warmup_us is -1.
    if (warmup_us < 0){
        warmup_us = DEFAULT_WARMUP_US;
    }

    printf("Starting warmup period of %lu micro-seconds...\n\n", warmup_us);
    usleep(warmup_us);  // 

    printf("Awoken from warmup, will now enable statistics.\n");

    // Enable statistics
    
    // Reset all statistics to start fresh after warmup
    memset(&stats, 0, sizeof(stats));
    stats.min_latency = UINT64_MAX;
    stats.interval_min_latency = UINT64_MAX;
    reset_interval_stats();                 // sets interval_start_tsc to now

    stats_enabled = 1;

    printf("Warmup finished, initializing statistics collection.\n");

    // Print statistics every second
    int sCount = 100; // 100 iterations of 0.001s -> 1s
    int mainIterationCounter = 0;
    while (!force_quit) {
        //sleep(1);
        int sleep_count = 0; 
            while(!force_quit && sleep_count < sCount){
            usleep(10000);
            sleep_count++;
        }
        if (force_quit){
            break;
        }
        if(sleep_count >= sCount){
            printf("mainIterationCounter is now : %d\n", mainIterationCounter);
            mainIterationCounter++;
#ifdef DEBUG
            print_interval_stats();
            printf("-n targetMaxToSend: %ld ; done %ld \n", total_packets, stats.tx);
            // Check if we've reached the target packet count
            if (total_packets > 0 && stats.tx >= total_packets && !force_quit) {
                printf("Reached target packet count: %lu\n", total_packets);
                force_quit = true;
            }
#endif
        }
    }

    printf("Main loop exited since force_quit was set to true; is now waiting for worker threads...\n");
    rte_eal_mp_wait_lcore();
    printf("All worker threads have completed, will now print overall stats\n");

    // Calculate and print overall statistics
    calculate_overall_stats();
    print_overall_stats();

    usleep(500);
    rte_eal_cleanup();
    usleep(100);
    fflush(stdout);
    usleep(300);
    return 0;
}



// // expected : 
// //     sudo ./latency_test -l 2,4,6 -- -B 32 -s 256 -p tlogn -T -16.812 0.336 -9.904 0.336 -14.509 1.386
// // 
// // Mean ON duration  = 0.000050 s (     50.00 us)
// // Mean OFF duration = 0.000050 s (     50.00 us)
// // Mean inter‑burst  = 0.000001 s (      1.00 us)
// // Expected packet rate (ON phase) = 32'000'000.0 pps
// // Long‑term average packet rate   = 16'000'000.0 pps


