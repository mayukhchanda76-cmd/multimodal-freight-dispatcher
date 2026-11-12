#include "mfyd.h"

void seed_demo_routes_if_empty(void) {
    FILE *fp = fopen(ROUTES_FILE, "rb");
    if (fp == NULL) {
        /* File doesn't exist yet, let's create it with demo corridors */
        FreightRoute r1 = {1, "Central Terminal", "North Inland Port", MODE_RAIL, 120.0f, 0.0f, 45.0f};
        FreightRoute r2 = {2, "Central Terminal", "East Sea Hub",      MODE_ROAD, 40.0f,  0.0f, 75.0f};
        FreightRoute r3 = {3, "Central Terminal", "West Logistics Park", MODE_RAIL, 200.0f, 0.0f, 35.0f};

        add_freight_route(&r1);
        add_freight_route(&r2);
        add_freight_route(&r3);
        printf("[*] Seeded 3 demo routes into routes.dat\n");
    } else {
        fclose(fp);
    }
}