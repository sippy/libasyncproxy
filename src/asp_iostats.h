struct asp_iostats_uni {
    uint64_t nops;
    uint64_t btotal;
};

struct asp_iostats_bi {
    struct asp_iostats_uni in;
    struct asp_iostats_uni out;
};

