
#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

/*
    INVENTORY PRO - C + raylib only
    --------------------------------
    No SQL / SQLite / C++.

    Persistence:
      inventory.dat  - products, suppliers, users, sales, purchase orders
      audit.dat      - append-only audit records
      stock.dat      - append-only stock movement records

    Default accounts:
      admin / admin123
      staff / staff123

    Build:
      cc -std=c11 -O2 main.c -o inventory -lraylib -lm
*/

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define SCREEN_W 1440
#define SCREEN_H 900
#define MAX_PRODUCTS 1000
#define MAX_SUPPLIERS 300
#define MAX_USERS 50
#define MAX_SALES 2000
#define MAX_SALE_ITEMS 10000
#define MAX_PURCHASES 1000
#define MAX_PURCHASE_ITEMS 5000
#define MAX_MOVEMENTS 10000
#define MAX_AUDIT 20000
#define MAX_STR 256
#define PAGE_SIZE 12

typedef struct {
    int id;
    char sku[64], name[128], description[MAX_STR], category[64], brand[64];
    int supplier_id;
    char barcode[64], expiry[32], location[64], image_path[MAX_STR];
    float purchase_price, selling_price;
    int quantity, min_stock;
    int archived;
    long created_at;
} Product;

typedef struct {
    int id;
    char name[128], email[128], phone[64], address[MAX_STR];
    int archived;
} Supplier;

typedef struct {
    int id;
    char username[64], password_hash[65], role[16];
    int active;
} User;

typedef struct {
    int product_id, quantity;
    float unit_price, total;
} SaleItem;

typedef struct {
    int id, user_id;
    char order_no[64], date[32], status[24];
    float total;
    int first_item, item_count;
} Sale;

typedef struct {
    int product_id, quantity, received;
    float unit_price;
} PurchaseItem;

typedef struct {
    int id, supplier_id, user_id;
    char order_no[64], date[32], status[32];
    int first_item, item_count;
} PurchaseOrder;

typedef struct {
    long id, timestamp;
    int product_id, user_id;
    int change, old_qty, new_qty;
    char type[32], reason[MAX_STR];
} StockMovement;

typedef struct {
    long id, timestamp;
    int user_id;
    char action[64], details[MAX_STR];
} Audit;

static Product products[MAX_PRODUCTS]; static int product_count;
static Supplier suppliers[MAX_SUPPLIERS]; static int supplier_count;
static User users[MAX_USERS]; static int user_count;
static Sale sales[MAX_SALES]; static int sale_count;
static SaleItem sale_items[MAX_SALE_ITEMS]; static int sale_item_count;
static PurchaseOrder purchases[MAX_PURCHASES]; static int purchase_count;
static PurchaseItem purchase_items[MAX_PURCHASE_ITEMS]; static int purchase_item_count;
static StockMovement movements[MAX_MOVEMENTS]; static int movement_count;
static Audit audits[MAX_AUDIT]; static int audit_count;

static int next_product_id = 1, next_supplier_id = 1, next_user_id = 1;
static int next_sale_id = 1, next_purchase_id = 1;
static long next_movement_id = 1, next_audit_id = 1;

static int logged_user = 0;
static char logged_name[64], logged_role[16];
static int page = 0;
static int modal = 0;
static int selected_id = 0;
static int page_no = 0, sort_mode = 0, sort_desc = 0;
static int low_filter = 0, out_filter = 0;
static char search[128] = "";
static char form[14][MAX_STR];
static int focus = -1;
static char status_msg[256] = "";
static double status_until = 0;

enum {
    PAGE_DASH, PAGE_PRODUCTS, PAGE_STOCK, PAGE_SALES, PAGE_SUPPLIERS,
    PAGE_PURCHASES, PAGE_REPORTS, PAGE_AUDIT
};
enum {
    MOD_NONE, MOD_PRODUCT, MOD_SUPPLIER, MOD_STOCK, MOD_SALE,
    MOD_PURCHASE, MOD_LOGIN
};

static void set_msg(const char* s) {
    snprintf(status_msg, sizeof(status_msg), "%s", s);
    status_until = GetTime() + 4;
}
static void now_string(char* s, size_t n) {
    time_t t = time(NULL); struct tm* x = localtime(&t);
    strftime(s, n, "%Y-%m-%d %H:%M:%S", x);
}
static void date_string(char* s, size_t n) {
    time_t t = time(NULL); struct tm* x = localtime(&t);
    strftime(s, n, "%Y-%m-%d", x);
}
static int admin(void) { return strcmp(logged_role, "admin") == 0; }

static void hash_password(const char* p, char out[65]) {
    /* Small deterministic FNV-style hash for a self-contained C/raylib app.
       For production, replace with Argon2id/bcrypt via a crypto library. */
    uint64_t h1 = 1469598103934665603ULL, h2 = 1099511628211ULL;
    for (const unsigned char* s = (const unsigned char*)p;*s;s++) {
        h1 ^= *s; h1 *= 1099511628211ULL;
        h2 ^= (uint64_t)(*s + 31); h2 *= 1469598103934665603ULL;
    }
    snprintf(out, 65, "%016llx%016llx%016llx%016llx",
        (unsigned long long)h1, (unsigned long long)h2,
        (unsigned long long)(h1 ^ h2), (unsigned long long)(h1 + h2));
}

static void save_file(const char* fn, const void* data, size_t size, int count) {
    FILE* f = fopen(fn, "wb"); if (!f)return;
    fwrite(&count, sizeof(int), 1, f); fwrite(data, size, (size_t)count, f); fclose(f);
}
static int load_file(const char* fn, void* data, size_t size, int max) {
    FILE* f = fopen(fn, "rb"); if (!f)return 0;
    int n = 0; fread(&n, sizeof(int), 1, f); if (n < 0)n = 0;if (n > max)n = max;
    fread(data, size, (size_t)n, f);fclose(f);return n;
}

static void save_all(void) {
    FILE* f = fopen("inventory.dat", "wb");if (!f)return;
    fwrite("INV1", 1, 4, f);
    fwrite(&next_product_id, sizeof(next_product_id), 1, f);
    fwrite(&next_supplier_id, sizeof(next_supplier_id), 1, f);
    fwrite(&next_user_id, sizeof(next_user_id), 1, f);
    fwrite(&next_sale_id, sizeof(next_sale_id), 1, f);
    fwrite(&next_purchase_id, sizeof(next_purchase_id), 1, f);
    fwrite(&next_movement_id, sizeof(next_movement_id), 1, f);
    fwrite(&next_audit_id, sizeof(next_audit_id), 1, f);
#define ARR(a,c) fwrite(&(c),sizeof(int),1,f);fwrite((a),sizeof((a)[0]),(size_t)(c),f)
    ARR(products, product_count); ARR(suppliers, supplier_count); ARR(users, user_count);
    ARR(sales, sale_count); ARR(sale_items, sale_item_count);
    ARR(purchases, purchase_count); ARR(purchase_items, purchase_item_count);
    ARR(movements, movement_count); ARR(audits, audit_count);
#undef ARR
    fclose(f);
}
static int read_arr(FILE* f, void* a, size_t sz, int* count, int max) {
    int n = 0;if (fread(&n, sizeof(int), 1, f) != 1)return 0;if (n < 0)n = 0;if (n > max)n = max;
    fread(a, sz, (size_t)n, f);*count = n;return 1;
}
static int load_all(void) {
    FILE* f = fopen("inventory.dat", "rb");if (!f)return 0;
    char magic[5] = { 0 };fread(magic, 1, 4, f);if (strcmp(magic, "INV1")) { fclose(f);return 0; }
    fread(&next_product_id, sizeof(next_product_id), 1, f);
    fread(&next_supplier_id, sizeof(next_supplier_id), 1, f);
    fread(&next_user_id, sizeof(next_user_id), 1, f);
    fread(&next_sale_id, sizeof(next_sale_id), 1, f);
    fread(&next_purchase_id, sizeof(next_purchase_id), 1, f);
    fread(&next_movement_id, sizeof(next_movement_id), 1, f);
    fread(&next_audit_id, sizeof(next_audit_id), 1, f);
    int ok = 1;
    ok &= read_arr(f, products, sizeof(Product), &product_count, MAX_PRODUCTS);
    ok &= read_arr(f, suppliers, sizeof(Supplier), &supplier_count, MAX_SUPPLIERS);
    ok &= read_arr(f, users, sizeof(User), &user_count, MAX_USERS);
    ok &= read_arr(f, sales, sizeof(Sale), &sale_count, MAX_SALES);
    ok &= read_arr(f, sale_items, sizeof(SaleItem), &sale_item_count, MAX_SALE_ITEMS);
    ok &= read_arr(f, purchases, sizeof(PurchaseOrder), &purchase_count, MAX_PURCHASES);
    ok &= read_arr(f, purchase_items, sizeof(PurchaseItem), &purchase_item_count, MAX_PURCHASE_ITEMS);
    ok &= read_arr(f, movements, sizeof(StockMovement), &movement_count, MAX_MOVEMENTS);
    ok &= read_arr(f, audits, sizeof(Audit), &audit_count, MAX_AUDIT);
    fclose(f);return ok;
}

static void audit_add(const char* action, const char* details) {
    if (audit_count >= MAX_AUDIT)return;
    Audit* a = &audits[audit_count++];a->id = next_audit_id++;a->timestamp = time(NULL);
    a->user_id = logged_user;snprintf(a->action, sizeof(a->action), "%s", action);
    snprintf(a->details, sizeof(a->details), "%s", details);save_all();
}
static Product* product_by_id(int id) { for (int i = 0;i < product_count;i++)if (products[i].id == id)return &products[i];return NULL; }
static Supplier* supplier_by_id(int id) { for (int i = 0;i < supplier_count;i++)if (suppliers[i].id == id)return &suppliers[i];return NULL; }

static void stock_change(int pid, int delta, const char* type, const char* reason) {
    Product* p = product_by_id(pid);if (!p)return;
    int old = p->quantity, newq = old + delta;
    if (newq < 0) { set_msg("Stock cannot become negative.");return; }
    p->quantity = newq;
    if (movement_count < MAX_MOVEMENTS) {
        StockMovement* m = &movements[movement_count++];
        m->id = next_movement_id++;m->timestamp = time(NULL);m->product_id = pid;m->user_id = logged_user;
        m->change = delta;m->old_qty = old;m->new_qty = newq;
        snprintf(m->type, sizeof(m->type), "%s", type);
        snprintf(m->reason, sizeof(m->reason), "%s", reason ? reason : "");
    }
    char d[256];snprintf(d, sizeof(d), "SKU=%s old=%d new=%d reason=%s", p->sku, old, newq, reason ? reason : "");
    audit_add(delta >= 0 ? "STOCK_INCREASE" : "STOCK_DECREASE", d);
    save_all();
}

static void copy_string(char* dest, size_t dest_size, const char* src)
{
    if (dest_size == 0) return;

    size_t i = 0;
    while (i < dest_size - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }

    dest[i] = '\0';
}

static void seed_data(void) {
    if (user_count == 0) {
        User* u = &users[user_count++]; u->id = next_user_id++; copy_string(u->username, sizeof(u->username), "admin"); copy_string(u->role, sizeof(u->role), "admin"); u->active = 1; hash_password("admin123", u->password_hash);
        u = &users[user_count++]; u->id = next_user_id++; copy_string(u->username, sizeof(u->username), "staff"); copy_string(u->role, sizeof(u->role), "staff"); u->active = 1; hash_password("staff123", u->password_hash);
    }
    if (supplier_count == 0) {
        Supplier* s = &suppliers[supplier_count++]; s->id = next_supplier_id++; copy_string(s->name, sizeof(s->name), "Acme Wholesale"); copy_string(s->email, sizeof(s->email), "sales@acme.example"); copy_string(s->phone, sizeof(s->phone), "01 555 0100"); copy_string(s->address, sizeof(s->address), "Dublin");
        s = &suppliers[supplier_count++]; s->id = next_supplier_id++; copy_string(s->name, sizeof(s->name), "Green Foods"); copy_string(s->email, sizeof(s->email), "orders@green.example"); copy_string(s->phone, sizeof(s->phone), "01 555 0200"); copy_string(s->address, sizeof(s->address), "Cork");
    }
    if (product_count == 0) {
        Product* p = &products[product_count++]; memset(p, 0, sizeof(*p)); p->id = next_product_id++; copy_string(p->sku, sizeof(p->sku), "SKU-1001"); copy_string(p->barcode, sizeof(p->barcode), "5010001001"); copy_string(p->name, sizeof(p->name), "Coffee Beans"); copy_string(p->description, sizeof(p->description), "Arabica beans 1kg"); copy_string(p->category, sizeof(p->category), "Beverages"); copy_string(p->brand, sizeof(p->brand), "Roaster Co"); p->supplier_id = 1; copy_string(p->expiry, sizeof(p->expiry), "2027-12-31"); copy_string(p->location, sizeof(p->location), "A-01"); p->purchase_price = 9.5f; p->selling_price = 14.99f; p->quantity = 42; p->min_stock = 10; p->created_at = time(NULL);
        p = &products[product_count++]; memset(p, 0, sizeof(*p)); p->id = next_product_id++; copy_string(p->sku, sizeof(p->sku), "SKU-1002"); copy_string(p->barcode, sizeof(p->barcode), "5010001002"); copy_string(p->name, sizeof(p->name), "Milk 1L"); copy_string(p->description, sizeof(p->description), "Whole milk"); copy_string(p->category, sizeof(p->category), "Dairy"); copy_string(p->brand, sizeof(p->brand), "Green Foods"); p->supplier_id = 2; copy_string(p->expiry, sizeof(p->expiry), "2026-11-20"); copy_string(p->location, sizeof(p->location), "B-03"); p->purchase_price = 0.8f; p->selling_price = 1.49f; p->quantity = 7; p->min_stock = 10; p->created_at = time(NULL);
        p = &products[product_count++]; memset(p, 0, sizeof(*p)); p->id = next_product_id++; copy_string(p->sku, sizeof(p->sku), "SKU-1003"); copy_string(p->barcode, sizeof(p->barcode), "5010001003"); copy_string(p->name, sizeof(p->name), "Notebook"); copy_string(p->description, sizeof(p->description), "A5 ruled notebook"); copy_string(p->category, sizeof(p->category), "Stationery"); copy_string(p->brand, sizeof(p->brand), "PaperCo"); p->supplier_id = 1; copy_string(p->location, sizeof(p->location), "C-02"); p->purchase_price = 1.2f; p->selling_price = 3.5f; p->quantity = 0; p->min_stock = 5; p->created_at = time(NULL);
    }
    save_all();
}

static int login(const char* name, const char* pass) {
    char h[65];hash_password(pass, h);
    for (int i = 0;i < user_count;i++)if (users[i].active && !strcmp(users[i].username, name) && !strcmp(users[i].password_hash, h)) {
        logged_user = users[i].id;copy_string(logged_name, sizeof(logged_name), users[i].username);copy_string(logged_role, sizeof(logged_role), users[i].role);audit_add("LOGIN", "User logged in");return 1;
    }return 0;
}

static void draw_text(const char* s, float x, float y, int sz, Color c) { DrawText(s, (int)x, (int)y, sz, c); }
static int button(Rectangle r, const char* s, int active) {
    DrawRectangleRec(r, active ? (Color) { 45, 110, 190, 255 } : (Color) { 40, 45, 55, 255 });
    DrawRectangleLinesEx(r, 1, (Color) { 80, 90, 105, 255 });
    int w = MeasureText(s, 17);draw_text(s, r.x + r.width / 2 - w / 2, r.y + 9, 17, RAYWHITE);
    return CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static int text_input(Rectangle r, char* s, int cap, int active) {
    DrawRectangleRec(r, (Color) { 24, 28, 35, 255 });DrawRectangleLinesEx(r, 1, active ? SKYBLUE : (Color) { 70, 75, 85, 255 });
    draw_text(s, r.x + 8, r.y + 8, 17, RAYWHITE);
    if (active) { int c = GetCharPressed();while (c) { if (c >= 32 && c <= 125 && (int)strlen(s) < cap - 1) { int n = (int)strlen(s);s[n] = (char)c;s[n + 1] = 0; }c = GetCharPressed(); }if (IsKeyPressed(KEY_BACKSPACE)) { int n = (int)strlen(s);if (n)s[n - 1] = 0; } }
    return CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
static void header(void) {
    DrawRectangle(0, 0, SCREEN_W, 64, (Color) { 22, 27, 34, 255 });draw_text("INVENTORY PRO", 22, 14, 27, WHITE);
    draw_text(logged_name, SCREEN_W - 220, 12, 16, LIGHTGRAY);draw_text(logged_role, SCREEN_W - 220, 34, 12, GRAY);
    if (button((Rectangle) { SCREEN_W - 95, 14, 75, 36 }, "Logout", 0)) { audit_add("LOGOUT", "User logged out");logged_user = 0;logged_name[0] = 0;logged_role[0] = 0; }
}
static void sidebar(void) {
    DrawRectangle(0, 64, 210, SCREEN_H - 64, (Color) { 18, 22, 28, 255 });
    const char* n[] = { "Dashboard","Products","Stock","Sales","Suppliers","Purchases","Reports","Audit Log" };
    for (int i = 0;i < 8;i++)if (button((Rectangle) { 14, 84 + i * 50, 182, 40 }, n[i], page == i)) { page = i;page_no = 0; }
    draw_text("Role:", 18, 520, 13, GRAY);draw_text(admin() ? "Administrator" : "Staff", 18, 542, 14, LIGHTGRAY);
}

static int total_units(void) { int x = 0;for (int i = 0;i < product_count;i++)if (!products[i].archived)x += products[i].quantity;return x; }
static int low_count(void) { int x = 0;for (int i = 0;i < product_count;i++)if (!products[i].archived && products[i].quantity > 0 && products[i].quantity <= products[i].min_stock)x++;return x; }
static int out_count(void) { int x = 0;for (int i = 0;i < product_count;i++)if (!products[i].archived && products[i].quantity == 0)x++;return x; }
static float inventory_value(void) { float x = 0;for (int i = 0;i < product_count;i++)if (!products[i].archived)x += products[i].quantity * products[i].purchase_price;return x; }

static void kpi(const char* a, const char* b, float x, float y) {
    DrawRectangle(x, y, 270, 100, (Color) { 28, 34, 42, 255 });DrawRectangleLines(x, y, 270, 100, (Color) { 55, 65, 78, 255 });
    draw_text(a, x + 16, y + 14, 15, GRAY);draw_text(b, x + 16, y + 43, 27, RAYWHITE);
}
static void dashboard(void) {
    draw_text("Dashboard", 240, 90, 30, WHITE);
    kpi("Total products", TextFormat("%d", product_count), 240, 135);
    kpi("Units in stock", TextFormat("%d", total_units()), 530, 135);
    kpi("Low-stock", TextFormat("%d", low_count()), 820, 135);
    kpi("Out of stock", TextFormat("%d", out_count()), 1110, 135);
    kpi("Inventory value", TextFormat("€ %.2f", inventory_value()), 240, 250);
    kpi("Sales recorded", TextFormat("%d", sale_count), 530, 250);
    kpi("Stock movements", TextFormat("%d", movement_count), 820, 250);

    draw_text("Sales trend - last 7 days", 240, 385, 22, WHITE);
    DrawRectangle(240, 420, 760, 250, (Color) { 24, 29, 36, 255 });
    float vals[7] = { 0 };float mx = 1;
    time_t now = time(NULL);
    for (int i = 0;i < sale_count;i++)for (int d = 0;d < 7;d++) {
        if ((now - sales[i].id) >= 0) { /* sale id is independent; date is displayed separately */
            char date[32];time_t tt = now - d * 86400;struct tm* tmv = localtime(&tt);strftime(date, sizeof(date), "%Y-%m-%d", tmv);
            if (!strcmp(sales[i].date, date)) { vals[6 - d] += sales[i].total;if (vals[6 - d] > mx)mx = vals[6 - d]; }
        }
    }
    for (int i = 0;i < 7;i++) { float bh = vals[i] / mx * 180;DrawRectangle(275 + i * 100, 640 - bh, 55, bh, SKYBLUE);draw_text(TextFormat("D-%d", 6 - i), 280 + i * 100, 648, 12, GRAY); }
    draw_text("Recent sales", 1030, 385, 22, WHITE);int y = 425;
    for (int i = sale_count - 1;i >= 0 && i >= sale_count - 6;i--) { draw_text(sales[i].order_no, 1030, y, 14, WHITE);draw_text(sales[i].date, 1140, y, 12, GRAY);draw_text(TextFormat("€ %.2f", sales[i].total), 1250, y, 14, LIGHTGRAY);y += 32; }
}

static void clear_form(void) { memset(form, 0, sizeof(form));focus = -1; }
static void start_modal(int m, int id) { modal = m;selected_id = id;clear_form(); }

static void product_form(void) {
    DrawRectangle(270, 80, 900, 760, (Color) { 20, 25, 31, 255 });DrawRectangleLines(270, 80, 900, 760, SKYBLUE);
    int edit = selected_id != 0;draw_text(edit ? "Edit Product" : "Add Product", 300, 105, 26, WHITE);
    const char* l[] = { "SKU","Barcode","Name","Description","Category","Brand","Expiry YYYY-MM-DD","Purchase price","Selling price","Quantity","Minimum stock","Location","Image path" };
    int yy = 150;for (int i = 0;i < 13;i++) { draw_text(l[i], 300, yy - 19, 12, GRAY);text_input((Rectangle) { 300, yy, 820, 34 }, form[i], MAX_STR, focus == i);yy += 49; }
    if (button((Rectangle) { 900, 785, 100, 38 }, "Cancel", 0))modal = MOD_NONE;
    if (button((Rectangle) { 1020, 785, 120, 38 }, edit ? "Save" : "Add", 1)) {
        Product* p = edit ? product_by_id(selected_id) : NULL;
        if (!edit) { if (product_count >= MAX_PRODUCTS) { set_msg("Product limit reached.");return; }p = &products[product_count++];memset(p, 0, sizeof(*p));p->id = next_product_id++;p->created_at = time(NULL); }
        snprintf(p->sku, 64, "%s", form[0]);snprintf(p->barcode, 64, "%s", form[1]);snprintf(p->name, 128, "%s", form[2]);snprintf(p->description, MAX_STR, "%s", form[3]);snprintf(p->category, 64, "%s", form[4]);snprintf(p->brand, 64, "%s", form[5]);snprintf(p->expiry, 32, "%s", form[6]);
        p->purchase_price = (float)atof(form[7]);p->selling_price = (float)atof(form[8]);p->quantity = atoi(form[9]);p->min_stock = atoi(form[10]);snprintf(p->location, 64, "%s", form[11]);snprintf(p->image_path, MAX_STR, "%s", form[12]);
        if (p->quantity < 0)p->quantity = 0;if (p->min_stock < 0)p->min_stock = 0;
        audit_add(edit ? "PRODUCT_UPDATED" : "PRODUCT_CREATED", p->sku);save_all();set_msg(edit ? "Product updated." : "Product added.");modal = MOD_NONE;
    }
}

static int product_matches(Product* p) {
    if (p->archived)return 0;
    if (search[0] && !strstr(p->name, search) && !strstr(p->sku, search) && !strstr(p->barcode, search))return 0;
    if (low_filter && !(p->quantity > 0 && p->quantity <= p->min_stock))return 0;
    if (out_filter && p->quantity != 0)return 0;
    return 1;
}
static int product_cmp(const void* A, const void* B) {
    Product* a = (Product*)A, * b = (Product*)B;int c = 0;
    if (sort_mode == 0)c = strcasecmp(a->name, b->name);
    else if (sort_mode == 1)c = a->quantity - b->quantity;
    else if (sort_mode == 2)c = (a->selling_price > b->selling_price) - (a->selling_price < b->selling_price);
    else c = (a->created_at > b->created_at) - (a->created_at < b->created_at);
    return sort_desc ? -c : c;
}
static void products_page(void) {
    draw_text("Products", 240, 90, 30, WHITE);
    if (admin() && button((Rectangle) { 1040, 85, 130, 40 }, "+ Add", 1)) { start_modal(MOD_PRODUCT, 0); }
    text_input((Rectangle) { 240, 140, 300, 38 }, search, 127, focus == 30);draw_text("Name / SKU / barcode", 240, 121, 12, GRAY);
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 240, 140, 300, 38 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))focus = 30;
    if (button((Rectangle) { 550, 140, 100, 38 }, "Clear", 0)) { search[0] = 0;low_filter = out_filter = 0;page_no = 0; }
    if (button((Rectangle) { 670, 140, 120, 38 }, low_filter ? "Low ON" : "Low", low_filter))low_filter = !low_filter;
    if (button((Rectangle) { 800, 140, 120, 38 }, out_filter ? "Out ON" : "Out", out_filter))out_filter = !out_filter;
    if (button((Rectangle) { 930, 140, 120, 38 }, "Name", sort_mode == 0)) { if (sort_mode == 0)sort_desc = !sort_desc;sort_mode = 0; }
    if (button((Rectangle) { 1060, 140, 120, 38 }, "Quantity", sort_mode == 1)) { if (sort_mode == 1)sort_desc = !sort_desc;sort_mode = 1; }

    Product tmp[MAX_PRODUCTS];int n = 0;for (int i = 0;i < product_count;i++)if (product_matches(&products[i]))tmp[n++] = products[i];qsort(tmp, n, sizeof(Product), product_cmp);
    int start = page_no * PAGE_SIZE;if (start >= n && page_no > 0) { page_no--;start = page_no * PAGE_SIZE; }
    int y = 210;draw_text("SKU", 250, y, 13, GRAY);draw_text("Name", 340, y, 13, GRAY);draw_text("Category", 570, y, 13, GRAY);draw_text("Qty", 720, y, 13, GRAY);draw_text("Min", 770, y, 13, GRAY);draw_text("Sell", 820, y, 13, GRAY);draw_text("Supplier", 900, y, 13, GRAY);draw_text("Expiry", 1050, y, 13, GRAY);draw_text("Actions", 1190, y, 13, GRAY);y += 28;
    for (int i = start;i < n && i < start + PAGE_SIZE;i++) {
        Product* p = &tmp[i];Color bg = p->quantity == 0 ? (Color) { 70, 35, 35, 255 } : p->quantity <= p->min_stock ? (Color) { 65, 58, 30, 255 } : (Color) { 28, 34, 42, 255 };DrawRectangle(240, y - 5, 1160, 38, bg);
        Supplier* s = supplier_by_id(p->supplier_id);draw_text(p->sku, 250, y, 13, WHITE);draw_text(p->name, 340, y, 13, WHITE);draw_text(p->category, 570, y, 13, WHITE);draw_text(TextFormat("%d", p->quantity), 720, y, 13, WHITE);draw_text(TextFormat("%d", p->min_stock), 770, y, 13, WHITE);draw_text(TextFormat("%.2f", p->selling_price), 820, y, 13, WHITE);draw_text(s ? s->name : "-", 900, y, 13, WHITE);draw_text(p->expiry, 1050, y, 13, WHITE);
        if (admin() && button((Rectangle) { 1175, y - 7, 75, 30 }, "Edit", 0)) { selected_id = p->id;for (int j = 0;j < 13;j++)form[j][0] = 0;snprintf(form[0], MAX_STR, "%s", p->sku);snprintf(form[1], MAX_STR, "%s", p->barcode);snprintf(form[2], MAX_STR, "%s", p->name);snprintf(form[3], MAX_STR, "%s", p->description);snprintf(form[4], MAX_STR, "%s", p->category);snprintf(form[5], MAX_STR, "%s", p->brand);snprintf(form[6], MAX_STR, "%s", p->expiry);snprintf(form[7], MAX_STR, "%.2f", p->purchase_price);snprintf(form[8], MAX_STR, "%.2f", p->selling_price);snprintf(form[9], MAX_STR, "%d", p->quantity);snprintf(form[10], MAX_STR, "%d", p->min_stock);snprintf(form[11], MAX_STR, "%s", p->location);snprintf(form[12], MAX_STR, "%s", p->image_path);modal = MOD_PRODUCT; }
        if (admin() && button((Rectangle) { 1260, y - 7, 85, 30 }, "Archive", 0)) { p->archived = 1;audit_add("PRODUCT_ARCHIVED", p->sku);save_all();set_msg("Product archived."); }y += 43;
    }
    if (button((Rectangle) { 1000, 790, 80, 36 }, "Prev", 0) && page_no > 0)page_no--;if (button((Rectangle) { 1090, 790, 80, 36 }, "Next", 0) && start + PAGE_SIZE < n)page_no++;draw_text(TextFormat("Page %d", page_no + 1), 1190, 800, 14, GRAY);
    if (focus == 30) { int c = GetCharPressed();while (c) { if (c >= 32 && c <= 125 && (int)strlen(search) < 127) { int z = strlen(search);search[z] = (char)c;search[z + 1] = 0; }c = GetCharPressed(); }if (IsKeyPressed(KEY_BACKSPACE)) { int z = strlen(search);if (z)search[z - 1] = 0; } }
}

static void stock_modal(void) {
    DrawRectangle(400, 250, 640, 340, (Color) { 20, 25, 31, 255 });DrawRectangleLines(400, 250, 640, 340, SKYBLUE);draw_text("Adjust Stock", 430, 275, 26, WHITE);
    draw_text("Delta (+ receive / - remove)", 430, 330, 14, GRAY);text_input((Rectangle) { 430, 350, 300, 40 }, form[0], 32, focus == 0);
    draw_text("Reason", 430, 415, 14, GRAY);text_input((Rectangle) { 430, 435, 540, 40 }, form[1], MAX_STR, focus == 1);
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 430, 350, 300, 40 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))focus = 0;
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 430, 435, 540, 40 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))focus = 1;
    if (button((Rectangle) { 740, 510, 120, 40 }, "Cancel", 0))modal = MOD_NONE;
    if (button((Rectangle) { 875, 510, 120, 40 }, "Apply", 1)) { int d = atoi(form[0]);Product* p = product_by_id(selected_id);if (!p) { modal = MOD_NONE;return; }if (d == 0)set_msg("Enter a non-zero adjustment.");else if (p->quantity + d < 0)set_msg("Stock cannot become negative.");else { stock_change(selected_id, d, d > 0 ? "increase" : "decrease", form[1]);set_msg("Stock adjusted.");modal = MOD_NONE; } }
}
static void stock_page(void) {
    draw_text("Stock Management", 240, 90, 30, WHITE);
    draw_text("Manual corrections and movement history", 240, 130, 15, GRAY);

    int y = 180;
    for (int i = 0; i < product_count && y < 520; i++) {
        Product* p = &products[i];
        if (p->archived) continue;
        draw_text(p->sku, 250, y, 14, WHITE);
        draw_text(p->name, 360, y, 14, WHITE);
        draw_text(TextFormat("%d", p->quantity), 760, y, 14, p->quantity == 0 ? RED : p->quantity <= p->min_stock ? YELLOW : WHITE);
        draw_text(TextFormat("min %d", p->min_stock), 820, y, 13, GRAY);
        if (button((Rectangle) { 980, y - 7, 130, 30 }, "Adjust", 0)) {
            start_modal(MOD_STOCK, p->id);
        }
        y += 42;
    }

    draw_text("Recent movements", 240, 560, 22, WHITE);
    y = 600;
    for (int i = movement_count - 1; i >= 0 && i >= movement_count - 7; i--) {
        StockMovement* m = &movements[i];
        Product* p = product_by_id(m->product_id);

        struct tm timeinfo;
        char ts[32];

        localtime_s(&timeinfo, &m->timestamp);

        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &timeinfo);

        draw_text(ts, 250, y, 12, GRAY);
        draw_text(p ? p->sku : "?", 400, y, 13, WHITE);
        draw_text(m->type, 500, y, 13, WHITE);
        draw_text(TextFormat("%d", m->change), 620, y, 13, WHITE);
        draw_text(TextFormat("%d -> %d", m->old_qty, m->new_qty), 680, y, 13, WHITE);
        draw_text(m->reason, 840, y, 13, GRAY);
        y += 34;
    }
}

static void create_sale(int pid, int qty) {
    Product* p = product_by_id(pid);
    if (!p || qty <= 0) {
        set_msg("Invalid product or quantity.");
        return;
    }
    if (p->quantity < qty) {
        set_msg("Insufficient stock.");
        return;
    }
    if (sale_count >= MAX_SALES || sale_item_count >= MAX_SALE_ITEMS) {
        set_msg("Sales storage full.");
        return;
    }

    Sale* s = &sales[sale_count++];
    memset(s, 0, sizeof(*s));
    s->id = next_sale_id++;
    s->user_id = logged_user;
    date_string(s->date, sizeof(s->date));
    snprintf(s->order_no, sizeof(s->order_no), "ORD-%d", s->id);

    copy_string(s->status, sizeof(s->status), "completed");

    s->total = p->selling_price * qty;
    s->first_item = sale_item_count;
    s->item_count = 1;

    sale_items[sale_item_count++] = (SaleItem){ pid, qty, p->selling_price, p->selling_price * qty };
    stock_change(pid, -qty, "sale", "Completed sale");
    audit_add("SALE_CREATED", s->order_no);
    save_all();
    set_msg("Sale completed.");
}

static void sales_page(void) {
    draw_text("Sales", 240, 90, 30, WHITE);
    draw_text("Product ID", 240, 140, 13, GRAY);
    text_input((Rectangle) { 240, 160, 150, 38 }, form[0], 32, focus == 0);
    draw_text("Quantity", 420, 140, 13, GRAY);
    text_input((Rectangle) { 420, 160, 120, 38 }, form[1], 32, focus == 1);

    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 240, 160, 150, 38 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        focus = 0;
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 420, 160, 120, 38 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        focus = 1;

    if (button((Rectangle) { 570, 160, 160, 38 }, "Complete Sale", 1)) {
        create_sale(atoi(form[0]), atoi(form[1]));
        form[0][0] = form[1][0] = 0;
    }

    draw_text("Previous orders", 240, 270, 22, WHITE);
    int y = 315;
    for (int i = sale_count - 1; i >= 0 && i >= sale_count - 10; i--) {
        Sale* s = &sales[i];
        draw_text(s->order_no, 250, y, 14, WHITE);
        draw_text(s->date, 390, y, 13, GRAY);
        draw_text(TextFormat("€ %.2f", s->total), 550, y, 14, WHITE);
        draw_text(s->status, 690, y, 13, GRAY);

        if (strcmp(s->status, "returned") && button((Rectangle) { 800, y - 7, 100, 30 }, "Return", 0)) {
            for (int j = 0; j < s->item_count; j++) {
                SaleItem* it = &sale_items[s->first_item + j];
                stock_change(it->product_id, it->quantity, "return", "Accepted return");
            }

            copy_string(s->status, sizeof(s->status), "returned");

            audit_add("SALE_RETURN", s->order_no);
            save_all();
            set_msg("Return accepted.");
        }
        y += 38;
    }
}

static void supplier_form(void) {
    DrawRectangle(350, 190, 740, 470, (Color) { 20, 25, 31, 255 });
    DrawRectangleLines(350, 190, 740, 470, SKYBLUE);
    draw_text(selected_id ? "Edit Supplier" : "Add Supplier", 380, 220, 26, WHITE);

    const char* l[] = { "Name", "Email", "Phone", "Address" };
    for (int i = 0; i < 4; i++) {
        draw_text(l[i], 390, 270 + i * 70, 13, GRAY);
        text_input((Rectangle) { 390, 290 + i * 70, 620, 38 }, form[i], MAX_STR, focus == i);
        if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 390, 290 + i * 70, 620, 38 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            focus = i;
    }

    if (button((Rectangle) { 770, 590, 110, 40 }, "Cancel", 0))
        modal = MOD_NONE;

    if (button((Rectangle) { 895, 590, 110, 40 }, selected_id ? "Save" : "Add", 1)) {
        Supplier* s = selected_id ? supplier_by_id(selected_id) : NULL;
        if (!s) {
            s = &suppliers[supplier_count++];
            memset(s, 0, sizeof(*s));
            s->id = next_supplier_id++;
        }

        copy_string(s->name, sizeof(s->name), form[0]);
        copy_string(s->email, sizeof(s->email), form[1]);
        copy_string(s->phone, sizeof(s->phone), form[2]);
        copy_string(s->address, sizeof(s->address), form[3]);

        audit_add(selected_id ? "SUPPLIER_UPDATED" : "SUPPLIER_CREATED", s->name);
        save_all();
        set_msg("Supplier saved.");
        modal = MOD_NONE;
    }
}

static void suppliers_page(void) {
    draw_text("Suppliers", 240, 90, 30, WHITE);
    if (admin() && button((Rectangle) { 1050, 85, 130, 40 }, "+ Add", 1))
        start_modal(MOD_SUPPLIER, 0);

    int y = 160;
    draw_text("Name", 250, y, 13, GRAY);
    draw_text("Email", 500, y, 13, GRAY);
    draw_text("Phone", 760, y, 13, GRAY);
    draw_text("Address", 930, y, 13, GRAY);
    y += 35;

    for (int i = 0; i < supplier_count; i++) {
        Supplier* s = &suppliers[i];
        if (s->archived) continue;

        draw_text(s->name, 250, y, 14, WHITE);
        draw_text(s->email, 500, y, 13, WHITE);
        draw_text(s->phone, 760, y, 13, WHITE);
        draw_text(s->address, 930, y, 13, WHITE);

        if (admin() && button((Rectangle) { 1200, y - 7, 75, 30 }, "Edit", 0)) {
            selected_id = s->id;

            copy_string(form[0], sizeof(form[0]), s->name);
            copy_string(form[1], sizeof(form[1]), s->email);
            copy_string(form[2], sizeof(form[2]), s->phone);
            copy_string(form[3], sizeof(form[3]), s->address);

            focus = -1;
            modal = MOD_SUPPLIER;
        }
        y += 42;
    }
}

static void create_purchase(void) {
    if (purchase_count >= MAX_PURCHASES || purchase_item_count >= MAX_PURCHASE_ITEMS) {
        set_msg("Purchase storage full.");
        return;
    }

    PurchaseOrder* po = &purchases[purchase_count++];
    memset(po, 0, sizeof(*po));
    po->id = next_purchase_id++;
    po->supplier_id = atoi(form[0]);
    po->user_id = logged_user;
    date_string(po->date, sizeof(po->date));
    snprintf(po->order_no, sizeof(po->order_no), "PO-%d", po->id);

    copy_string(po->status, sizeof(po->status), "pending");

    po->first_item = purchase_item_count;
    po->item_count = 1;

    Product* p = product_by_id(atoi(form[1]));
    if (!p) {
        purchase_count--;
        set_msg("Product not found.");
        return;
    }

    purchase_items[purchase_item_count++] = (PurchaseItem){ p->id, atoi(form[2]), 0, p->purchase_price };
    audit_add("PURCHASE_ORDER_CREATED", po->order_no);
    save_all();
    set_msg("Purchase order created.");
}

static void receive_purchase(PurchaseOrder* po) {
    for (int j = 0; j < po->item_count; j++) {
        PurchaseItem* it = &purchase_items[po->first_item + j];
        if (!it->received) {
            stock_change(it->product_id, it->quantity, "purchase", "Goods received");
            it->received = 1;
        }
    }

    copy_string(po->status, sizeof(po->status), "completed");

    audit_add("PURCHASE_RECEIVED", po->order_no);
    save_all();
    set_msg("Goods received.");
}
static void purchases_page(void) {
    draw_text("Purchase Orders", 240, 90, 30, WHITE);draw_text("Supplier ID", 240, 140, 13, GRAY);text_input((Rectangle) { 240, 160, 130, 36 }, form[0], 32, focus == 0);draw_text("Product ID", 390, 140, 13, GRAY);text_input((Rectangle) { 390, 160, 130, 36 }, form[1], 32, focus == 1);draw_text("Quantity", 540, 140, 13, GRAY);text_input((Rectangle) { 540, 160, 110, 36 }, form[2], 32, focus == 2);
    for (int i = 0;i < 3;i++) { Rectangle r = { (float)(240 + i * 150),160,i == 2 ? 110 : 130,36 };if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))focus = i; }
    if (button((Rectangle) { 680, 160, 160, 36 }, "Create PO", 1))create_purchase();
    draw_text("Orders", 240, 260, 22, WHITE);int y = 300;for (int i = purchase_count - 1;i >= 0 && i >= purchase_count - 10;i--) { PurchaseOrder* po = &purchases[i];Supplier* s = supplier_by_id(po->supplier_id);draw_text(po->order_no, 250, y, 14, WHITE);draw_text(po->date, 390, y, 13, GRAY);draw_text(s ? s->name : "?", 520, y, 13, WHITE);draw_text(po->status, 760, y, 13, GRAY);if (strcmp(po->status, "completed") && button((Rectangle) { 900, y - 7, 120, 30 }, "Receive", 0))receive_purchase(po);y += 40; }
}

static void csv_header(FILE* f, const char* a) { fprintf(f, "%s", a); }
static void reports_page(void) {
    draw_text("Reports & Export", 240, 90, 30, WHITE);

    if (button((Rectangle) { 240, 145, 220, 42 }, "Current Stock CSV", 1)) {
        FILE* f = NULL;
        if (fopen_s(&f, "current_stock.csv", "w") == 0 && f != NULL) {
            csv_header(f, "SKU,Barcode,Name,Category,Quantity,MinStock,PurchasePrice,SellingPrice,Location\n");
            for (int i = 0; i < product_count; i++) {
                if (!products[i].archived) {
                    Product* p = &products[i];
                    fprintf(f, "%s,%s,\"%s\",%s,%d,%d,%.2f,%.2f,%s\n", p->sku, p->barcode, p->name, p->category, p->quantity, p->min_stock, p->purchase_price, p->selling_price, p->location);
                }
            }
            fclose(f);
            audit_add("REPORT_EXPORT", "current_stock.csv");
            set_msg("Exported current_stock.csv");
        }
    }

    if (button((Rectangle) { 480, 145, 220, 42 }, "Sales CSV", 1)) {
        FILE* f = NULL;
        if (fopen_s(&f, "sales.csv", "w") == 0 && f != NULL) {
            fprintf(f, "OrderNo,Date,Total,Status\n");
            for (int i = 0; i < sale_count; i++)
                fprintf(f, "%s,%s,%.2f,%s\n", sales[i].order_no, sales[i].date, sales[i].total, sales[i].status);
            fclose(f);
            audit_add("REPORT_EXPORT", "sales.csv");
            set_msg("Exported sales.csv");
        }
    }

    if (button((Rectangle) { 720, 145, 220, 42 }, "Movements CSV", 1)) {
        FILE* f = NULL;
        if (fopen_s(&f, "stock_movements.csv", "w") == 0 && f != NULL) {
            fprintf(f, "Timestamp,ProductID,Type,Change,OldQty,NewQty,Reason\n");
            for (int i = 0; i < movement_count; i++) {
                char t[32];
                struct tm timeinfo;

                localtime_s(&timeinfo, &movements[i].timestamp);
                strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", &timeinfo);

                fprintf(f, "%s,%d,%s,%d,%d,%d,\"%s\"\n", t, movements[i].product_id, movements[i].type, movements[i].change, movements[i].old_qty, movements[i].new_qty, movements[i].reason);
            }
            fclose(f);
            audit_add("REPORT_EXPORT", "stock_movements.csv");
            set_msg("Exported stock_movements.csv");
        }
    }

    if (button((Rectangle) { 960, 145, 220, 42 }, "Audit CSV", 1)) {
        FILE* f = NULL;
        if (fopen_s(&f, "audit_log.csv", "w") == 0 && f != NULL) {
            fprintf(f, "Timestamp,UserID,Action,Details\n");
            for (int i = 0; i < audit_count; i++) {
                char t[32];
                struct tm timeinfo;

                localtime_s(&timeinfo, &audits[i].timestamp);
                strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", &timeinfo);

                fprintf(f, "%s,%d,%s,\"%s\"\n", t, audits[i].user_id, audits[i].action, audits[i].details);
            }
            fclose(f);
            audit_add("REPORT_EXPORT", "audit_log.csv");
            set_msg("Exported audit_log.csv");
        }
    }

    draw_text("Current stock report", 240, 250, 22, WHITE);
    draw_text(TextFormat("Products: %d", product_count), 250, 290, 17, LIGHTGRAY);
    draw_text(TextFormat("Units: %d", total_units()), 250, 325, 17, LIGHTGRAY);
    draw_text(TextFormat("Inventory value: € %.2f", inventory_value()), 250, 360, 17, LIGHTGRAY);
    draw_text(TextFormat("Low stock: %d", low_count()), 250, 395, 17, LIGHTGRAY);
    draw_text(TextFormat("Out of stock: %d", out_count()), 250, 430, 17, LIGHTGRAY);

    draw_text("Best sellers", 650, 250, 22, WHITE);
    int y = 290;
    for (int k = 0; k < 8; k++) {
        int best = -1, bq = -1;
        for (int i = 0; i < product_count; i++) {
            int q = 0;
            for (int j = 0; j < sale_item_count; j++) {
                if (sale_items[j].product_id == products[i].id)
                    q += sale_items[j].quantity;
            }
            if (q > bq) {
                best = i;
                bq = q;
            }
        }
        if (best < 0 || bq <= 0) break;
        draw_text(products[best].name, 660, y, 15, WHITE);
        draw_text(TextFormat("%d sold", bq), 930, y, 14, GRAY);
        y += 32;
    }

    draw_text("No recent movement", 950, 520, 20, WHITE);
    y = 560;
    time_t n = time(NULL);
    for (int i = 0; i < product_count && y < 800; i++) {
        int found = 0;
        for (int j = movement_count - 1; j >= 0; j--) {
            if (movements[j].product_id == products[i].id && n - movements[j].timestamp < 30 * 86400) {
                found = 1;
                break;
            }
        }
        if (!found && !products[i].archived) {
            draw_text(products[i].name, 960, y, 14, WHITE);
            draw_text(TextFormat("qty %d", products[i].quantity), 1210, y, 13, GRAY);
            y += 30;
        }
    }
}

static void audit_page(void) {
    draw_text("Audit Log (read-only)", 240, 90, 30, WHITE);
    int y = 145;
    draw_text("Timestamp", 250, y, 13, GRAY);
    draw_text("User", 420, y, 13, GRAY);
    draw_text("Action", 540, y, 13, GRAY);
    draw_text("Details", 760, y, 13, GRAY);
    y += 30;

    for (int i = audit_count - 1; i >= 0 && i >= audit_count - 22; i--) {
        Audit* a = &audits[i];
        char t[32];
        struct tm timeinfo;

        localtime_s(&timeinfo, &a->timestamp);
        strftime(t, sizeof(t), "%Y-%m-%d %H:%M", &timeinfo);

        draw_text(t, 250, y, 12, GRAY);
        draw_text(TextFormat("%d", a->user_id), 420, y, 13, WHITE);
        draw_text(a->action, 540, y, 13, WHITE);
        draw_text(a->details, 760, y, 13, GRAY);
        y += 31;
    }
}

static void login_screen(void) {
    ClearBackground((Color) { 14, 18, 23, 255 });draw_text("INVENTORY PRO", 540, 150, 42, WHITE);draw_text("C + raylib only — no SQL", 570, 205, 18, GRAY);
    draw_text("Username", 500, 290, 14, GRAY);text_input((Rectangle) { 500, 315, 440, 44 }, form[0], 64, focus == 0);
    draw_text("Password", 500, 385, 14, GRAY);text_input((Rectangle) { 500, 410, 440, 44 }, form[1], 64, focus == 1);
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 500, 315, 440, 44 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))focus = 0;if (CheckCollisionPointRec(GetMousePosition(), (Rectangle) { 500, 410, 440, 44 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))focus = 1;
    if (button((Rectangle) { 500, 490, 440, 48 }, "Login", 1)) { if (login(form[0], form[1])) { form[0][0] = form[1][0] = 0;set_msg("Welcome."); } else set_msg("Invalid login."); }
    draw_text("First run: admin/admin123   |   staff/staff123", 500, 570, 14, GRAY);
    if (status_msg[0] && GetTime() < status_until)draw_text(status_msg, 500, 610, 15, RED);
}

static void modal_draw(void) { if (modal == MOD_PRODUCT)product_form();else if (modal == MOD_STOCK)stock_modal();else if (modal == MOD_SUPPLIER)supplier_form(); }

int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "Inventory Management - C + raylib");
    SetTargetFPS(60);load_all();seed_data();
    while (!WindowShouldClose()) {
        BeginDrawing();ClearBackground((Color) { 14, 18, 23, 255 });
        if (!logged_user)login_screen();else {
            header();sidebar();
            if (page == PAGE_DASH)dashboard();else if (page == PAGE_PRODUCTS)products_page();else if (page == PAGE_STOCK)stock_page();else if (page == PAGE_SALES)sales_page();else if (page == PAGE_SUPPLIERS)suppliers_page();else if (page == PAGE_PURCHASES)purchases_page();else if (page == PAGE_REPORTS)reports_page();else audit_page();
            if (modal)modal_draw();
            if (status_msg[0] && GetTime() < status_until) { DrawRectangle(430, 830, 580, 42, (Color) { 40, 45, 55, 245 });draw_text(status_msg, 450, 842, 15, WHITE); }
        }
        EndDrawing();
    }
    save_all();CloseWindow();return 0;
}
