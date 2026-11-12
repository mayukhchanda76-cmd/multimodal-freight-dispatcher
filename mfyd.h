#ifndef MFYD_H
#define MFYD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================= */
/*                          PERSISTENCE & SYSTEM CONSTANTS                   */
/* ========================================================================= */

#define CONSIGNMENTS_FILE       "consignments.dat"
#define ROUTES_FILE             "routes.dat"
#define YARD_FILE               "yard.dat"
#define INVOICES_FILE           "invoices.dat"
#define AUDIT_LOG_FILE          "audit.log"
#define MANIFEST_REPORT_FILE    "manifest.txt"

#define TOTAL_YARD_BAYS         20
#define HAZMAT_BAY_COUNT        5      /* Bays 1 to 5 (Capacity: 30 tons) */
#define STANDARD_BAY_COUNT      15     /* Bays 6 to 20 (Capacity: 15 tons) */

#define HAZMAT_BAY_CAPACITY     30.0f
#define STANDARD_BAY_CAPACITY   15.0f

#define HAZMAT_SURCHARGE_FEE    150.0f
#define PRIORITY_SURCHARGE_RATE 0.20f  /* 20% extra on base freight */

#define STR_LEN_DEFAULT         64
#define DATE_STR_LEN            32

/* ========================================================================= */
/*                          ENUMS & DATA STRUCTURES                         */
/* ========================================================================= */

/**
 * @brief Lifecycle states for tracked shipments.
 */
typedef enum {
    STATUS_REGISTERED = 0,   /* Consignment entered into intake registry */
    STATUS_STAGED_IN_YARD,   /* Assigned and held within a physical yard bay */
    STATUS_DISPATCHED,       /* Allocated to an active outbound freight route */
    STATUS_DELIVERED,        /* Successfully reached transit destination */
    STATUS_CANCELLED         /* Staged or dispatch state revoked */
} ShipmentStatus;

/**
 * @brief Supported multimodal transport modes.
 */
typedef enum {
    MODE_ROAD = 1,           /* High velocity, flexible routing */
    MODE_RAIL = 2            /* High capacity, bulk rail freight */
} TransitMode;

/**
 * @brief Cargo Consignment Entity (Consignment Intake Module).
 */
typedef struct {
    int id;                                /* Auto-generated sequential ID */
    char sender[STR_LEN_DEFAULT];          /* Shipper / Client name */
    char destination[STR_LEN_DEFAULT];     /* Destination hub or terminal */
    float weight_tons;                     /* Cargo mass (must be > 0.0) */
    int priority;                          /* 1 = Express, 0 = Standard */
    int is_hazardous;                      /* 1 = Regulated Hazmat, 0 = Non-Hazmat */
    ShipmentStatus status;                 /* Current lifecycle status */
    int assigned_route_id;                 /* Associated Route ID (-1 if unassigned) */
    int assigned_bay_id;                   /* Associated Yard Bay ID (-1 if unassigned) */
} Consignment;

/**
 * @brief Freight Route Corridor Entity (Capacity Dispatch Module).
 */
typedef struct {
    int route_id;                          /* Sequential Route Corridor ID */
    char origin[STR_LEN_DEFAULT];          /* Hub origin location */
    char destination[STR_LEN_DEFAULT];     /* Transit terminal destination */
    TransitMode mode;                      /* MODE_ROAD or MODE_RAIL */
    float max_capacity_tons;               /* Max vehicle/consist load limit */
    float booked_weight_tons;              /* Currently allocated freight tonnage */
    float rate_per_ton;                    /* Contracted tariff rate ($/ton) */
} FreightRoute;

/**
 * @brief Yard Bay Staging Slot Entity (Yard Allocation Module).
 */
typedef struct {
    int bay_id;                            /* Fixed slot slot index (1 to 20) */
    int is_hazardous_rated;                /* 1 = Hazmat rated, 0 = Standard */
    float max_weight_tons;                 /* 30.0t for Hazmat, 15.0t for Standard */
    int occupied;                          /* 0 = Free, 1 = Occupied */
    int consignment_id;                    /* Held consignment ID (-1 if free) */
    float current_load_tons;               /* Actual weight staged in bay */
} YardBay;

/**
 * @brief Commercial Billing & Waybill Entity (Invoicing Module).
 */
typedef struct {
    int invoice_id;                        /* Sequential Waybill Number */
    int consignment_id;                    /* Referenced Cargo ID */
    int route_id;                          /* Referenced Freight Corridor ID */
    float cargo_weight_tons;               /* Billed mass in tons */
    float rate_per_ton;                    /* Applied freight rate */
    float base_freight;                    /* weight * rate_per_ton */
    float priority_surcharge;              /* 20% surcharge if priority == 1 */
    float hazmat_surcharge;                /* $150 fixed surcharge if is_hazardous == 1 */
    float total_amount;                    /* Total commercial settlement */
    char invoice_date[DATE_STR_LEN];       /* Timestamp string */
} Waybill;

/* ========================================================================= */
/*       MODULE 1: CONSIGNMENT INTAKE & LIFECYCLE STATE MACHINE             */
/* ========================================================================= */

/**
 * @brief Auto-generates a unique sequential ID by checking file size, validates
 *        non-zero weight, sets status to STATUS_REGISTERED, and appends to consignments.dat.
 * @return Positive assigned consignment ID on success, or -1 on validation/write failure.
 */
int register_consignment(Consignment *c);

/**
 * @brief Reads consignments.dat sequentially to extract a single record matching the given ID.
 * @return 1 if found and copied into dest, 0 otherwise.
 */
int find_consignment_by_id(int id, Consignment *dest);

/**
 * @brief Locates the exact file offset using fseek() in "rb+" mode and updates the status
 *        field in-place without rewriting the entire binary file.
 * @return 1 on successful update, 0 if ID not found or write failed.
 */
int update_consignment_status(int id, ShipmentStatus new_status);

/**
 * @brief Pulls all shipments matching a target state into an array in memory.
 * @return Number of matching consignments retrieved.
 */
int filter_consignments_by_status(ShipmentStatus status, Consignment list[], int max_len);

/**
 * @brief Exports all registered shipments into a cleanly aligned plain-text tabular
 *        file (manifest.txt) using formatted fprintf.
 */
void export_manifest_report(const char *filename);

/* ========================================================================= */
/*            MODULE 2: CAPACITY DISPATCH & ROUTE OPTIMIZATION               */
/* ========================================================================= */

/**
 * @brief Appends a transit corridor record into routes.dat (validates max capacity > 0).
 * @return Positive assigned route ID on success, or -1 on failure.
 */
int add_freight_route(FreightRoute *r);

/**
 * @brief Searches routes.dat sequentially to find a route corridor by its ID.
 * @return 1 if found and copied to dest, 0 otherwise.
 */
int find_route_by_id(int route_id, FreightRoute *dest);

/**
 * @brief Evaluates business criteria to select the better mode:
 *        - Rail is prioritized if weight > 15.0 tons and non-urgent (priority != 1).
 *        - Road is prioritized for priority == 1 (Express) or smaller consignments (<= 15.0 tons).
 * @return MODE_ROAD (1) or MODE_RAIL (2).
 */
int recommend_transit_mode(float weight, int priority, int is_hazardous);

/**
 * @brief Checks if route has sufficient spare capacity: booked_weight + weight <= max_capacity.
 *        Updates route's booked_weight_tons using an in-place file rewrite.
 *        Updates consignment's assigned_route_id and flips status to STATUS_DISPATCHED.
 *        If staged in yard, frees the yard bay automatically.
 * @return 1 on successful dispatch, 0 on failure (e.g., overcapacity or invalid IDs).
 */
int dispatch_consignment_to_route(int consignment_id, int route_id);

/**
 * @brief Iterates through all routes, calculates utilization percentage (booked / max) * 100,
 *        and displays an ASCII progress bar (e.g., [||||||....] 60%).
 */
void list_routes_with_utilization(void);

/**
 * @brief Reverses a dispatch, rolls back the booked weight on the route, and sets the
 *        consignment back to STATUS_STAGED_IN_YARD (or STATUS_REGISTERED if no bay held).
 * @return 1 on successful rollback, 0 on failure.
 */
int cancel_dispatch(int consignment_id);

/* ========================================================================= */
/*            MODULE 3: YARD BAY ALLOCATION & CONSTRAINT CHECKER             */
/* ========================================================================= */

/**
 * @brief Checks if yard.dat exists; if not, seeds it with 20 bays:
 *        - Bays 1–5: Hazmat-rated with 30.0-ton capacity.
 *        - Bays 6–20: Standard with 15.0-ton capacity.
 * @return 1 if newly created/seeded, 0 if already existed, -1 on write error.
 */
int init_yard_layout_file(void);

/**
 * @brief Scans for an unoccupied bay adhering to safety & load limits:
 *        - If cargo is hazardous, rejects non-hazmat bays.
 *        - Checks weight <= bay.max_weight_tons.
 *        - Selects the smallest available bay that can hold cargo (Best-Fit heuristic).
 *        Updates the bay record to occupied and marks consignment as STATUS_STAGED_IN_YARD.
 * @return Assigned bay_id (1 to 20) on success, or -1 if no suitable bay is available.
 */
int allocate_best_fit_bay(int consignment_id, float weight, int is_hazardous);

/**
 * @brief Resets occupied = 0, sets consignment_id = -1, clears load = 0.0, and saves back to file.
 * @return 1 on success, 0 on failure.
 */
int vacate_yard_bay(int bay_id);

/**
 * @brief Searches yard.dat to tell the user the exact bay location of a stored shipment.
 * @return 1 if found and copied into dest, 0 otherwise.
 */
int lookup_bay_by_consignment(int consignment_id, YardBay *dest);

/**
 * @brief Displays a visual 2D console grid of the yard (e.g., [H1: BUSY] [H2: FREE] [S6: FREE]...)
 *        showing occupancy and current tonnage at a glance.
 */
void print_yard_2d_grid(void);

/* ========================================================================= */
/*          MODULE 4: COMMERCIAL RATING, INVOICING & DAILY LEDGER            */
/* ========================================================================= */

/**
 * @brief Retrieves cargo weight and route rate.
 *        Calculates base freight: weight * rate_per_ton.
 *        Adds surcharges: 20% extra if priority == 1, fixed fee ($150) if is_hazardous == 1.
 *        Appends record to invoices.dat.
 * @return 1 on success, 0 on failure.
 */
int generate_waybill_invoice(int consignment_id, Waybill *wb);

/**
 * @brief Generates an export file waybill_<id>.txt formatted like a commercial freight bill
 *        with bordered ASCII dividers, itemized rates, and financial breakdowns.
 */
void print_waybill_slip(int consignment_id);

/**
 * @brief Appends a line with a live timestamp to audit.log
 *        Format: [YYYY-MM-DD HH:MM:SS] ACTION - Details (Entity #ID)
 */
void log_audit_trail(const char *action_tag, int entity_id, const char *details);

/**
 * @brief Traverses invoices.dat and consignments.dat to compute revenue aggregations
 *        and operational metrics.
 */
void compute_daily_financial_ledger(float *total_revenue, float *avg_shipment_cost, int *hazmat_count);

/**
 * @brief Queries invoice records by associated cargo ID to review payment and fee breakdowns.
 * @return 1 if found and populated in dest, 0 otherwise.
 */
int search_invoice_by_consignment(int consignment_id, Waybill *dest);

#endif /* MFYD_H */