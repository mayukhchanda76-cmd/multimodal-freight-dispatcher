#ifndef MFYD_H
#define MFYD_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ========================================================================= */
/*                       PERSISTENCE & SYSTEM CONSTANTS                      */
/* ========================================================================= */

#define CONSIGNMENTS_FILE          "consignments.dat"
#define ROUTES_FILE                "routes.dat"
#define YARD_FILE                  "yard.dat"
#define INVOICES_FILE              "invoices.dat"
#define AUDIT_FILE                 "audit.log"
#define MANIFEST_FILE              "manifest.txt"

#define CONSIGNMENT_ID_BASE        1001
#define ROUTE_ID_BASE              101
#define INVOICE_ID_BASE            5001

#define YARD_BAY_COUNT             20
#define HAZMAT_BAY_COUNT           5       /* Bays 1..5  (H1..H5, 30 t) */
#define STANDARD_BAY_COUNT         15      /* Bays 6..20 (G1..G15, 15 t) */

#define HAZMAT_BAY_CAPACITY_TONS   30.0f
#define STANDARD_BAY_CAPACITY_TONS 15.0f

#define RAIL_WEIGHT_THRESHOLD_TONS 15.0f   /* > 15 t non-express -> Rail */

#define PRIORITY_EXPRESS           1
#define PRIORITY_STANDARD          2

#define HAZMAT_SURCHARGE_FEE       150.0f
#define PRIORITY_SURCHARGE_RATE    0.20f   /* 20% extra on base freight */

#define STR_LEN_DEFAULT            64
#define BAY_LABEL_LEN              8
#define DATE_STR_LEN               32

/* ========================================================================= */
/*                           ENUMS & RESULT CODES                            */
/* ========================================================================= */

typedef enum {
    MFYD_OK            =  0,
    MFYD_ERR_IO        = -1,
    MFYD_ERR_NOT_FOUND = -2,
    MFYD_ERR_INVALID   = -3,
    MFYD_ERR_STATE     = -4,
    MFYD_ERR_CAPACITY  = -5,
    MFYD_ERR_DUPLICATE = -6
} MfydResult;

typedef enum {
    STATUS_REGISTERED = 0,     /* Entered into intake registry */
    STATUS_STAGED_IN_YARD,     /* Held in a physical yard bay */
    STATUS_DISPATCHED,         /* Booked onto an outbound route */
    STATUS_IN_TRANSIT,         /* Departed hub / en route */
    STATUS_DELIVERED,          /* Delivered at destination */
    STATUS_COUNT
} ShipmentStatus;

typedef enum {
    MODE_ROAD = 1,             /* Express / lighter loads */
    MODE_RAIL = 2              /* High-capacity bulk freight */
} TransitMode;

/* ========================================================================= */
/*                              DATA STRUCTURES                              */
/* ========================================================================= */

typedef struct {
    int            id;
    char           description[STR_LEN_DEFAULT];
    char           origin[STR_LEN_DEFAULT];
    char           destination[STR_LEN_DEFAULT];
    float          weight_tons;
    int            priority;           /* PRIORITY_EXPRESS (1) or PRIORITY_STANDARD (2) */
    int            is_hazardous;       /* 1 = Hazmat, 0 = Non-Hazmat */
    ShipmentStatus status;
    int            assigned_route_id;  /* -1 if unassigned */
} Consignment;

typedef struct {
    int         route_id;
    char        origin[STR_LEN_DEFAULT];
    char        destination[STR_LEN_DEFAULT];
    TransitMode mode;                  /* MODE_ROAD or MODE_RAIL */
    float       max_capacity_tons;
    float       booked_weight_tons;
    float       rate_per_ton;
} FreightRoute;

typedef struct {
    int   bay_id;                      /* 1..YARD_BAY_COUNT */
    char  label[BAY_LABEL_LEN];        /* "H1".."H5", "G1".."G15" */
    int   is_hazmat_rated;             /* 1 = Hazmat rated, 0 = General */
    float max_weight_tons;
    int   occupied;                    /* 0 = Free, 1 = Occupied */
    int   consignment_id;              /* -1 if free */
    float current_load_tons;
} YardBay;

typedef struct {
    int   invoice_id;
    int   consignment_id;
    int   route_id;
    float weight_tons;
    float rate_per_ton;
    float base_freight;
    float priority_surcharge;
    float hazmat_fee;
    float total_amount;
    int   is_hazardous;                /* Cached for daily ledger reporting */
    int   is_active;                   /* 1 = Active, 0 = Voided */
    char  issued_at[DATE_STR_LEN];
} Waybill;

/* ========================================================================= */
/*                              SHARED UTILITIES                             */
/* ========================================================================= */

const char *mfyd_strerror(int code);
void        mfyd_timestamp(char *buf, size_t n, const char *fmt);
const char *status_to_string(ShipmentStatus s);
const char *mode_to_string(TransitMode m);
const char *priority_to_string(int priority);

/* ========================================================================= */
/*       MODULE 1: CONSIGNMENT INTAKE & LIFECYCLE STATE MACHINE              */
/* ========================================================================= */

int  is_valid_transition(ShipmentStatus from, ShipmentStatus to);
int  register_consignment(Consignment *c);
int  find_consignment_by_id(int id, Consignment *dest);
int  update_consignment_status(int id, ShipmentStatus new_status);
int  save_consignment(const Consignment *c);
int  filter_consignments_by_status(ShipmentStatus status, Consignment list[], int max_len);
void export_manifest_report(const char *filename);

/* ========================================================================= */
/*            MODULE 2: CAPACITY DISPATCH & ROUTE OPTIMIZATION               */
/* ========================================================================= */

int  add_freight_route(FreightRoute *r);
int  find_route_by_id(int route_id, FreightRoute *dest);
int  recommend_transit_mode(float weight, int priority, int is_hazardous);
int  dispatch_consignment_to_route(int consignment_id, int route_id);
void list_routes_with_utilization(void);
int  cancel_dispatch(int consignment_id);

/* ========================================================================= */
/*            MODULE 3: YARD BAY ALLOCATION & CONSTRAINT CHECKER             */
/* ========================================================================= */

int  init_yard_layout_file(void);
int  allocate_best_fit_bay(int consignment_id, float weight, int is_hazardous);
int  vacate_yard_bay(int bay_id);
int  lookup_bay_by_consignment(int consignment_id, YardBay *dest);
void print_yard_2d_grid(void);

/* ========================================================================= */
/*          MODULE 4: COMMERCIAL RATING, INVOICING & DAILY LEDGER            */
/* ========================================================================= */

int  generate_waybill_invoice(int consignment_id, Waybill *wb);
void print_waybill_slip(int consignment_id);
void log_audit_trail(const char *action_tag, int entity_id, const char *details);
void compute_daily_financial_ledger(float *total_revenue, float *avg_shipment_cost, int *hazmat_count);
int  search_invoice_by_consignment(int consignment_id, Waybill *dest);

#endif /* MFYD_H */