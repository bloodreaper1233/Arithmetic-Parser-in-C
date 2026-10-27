// Joyal Joshy 251ADB018
// (optional) https://github.com/your-username/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: C17 standard (stdio, stdlib, string, ctype, math, errno, dirent, sys/stat);
//                 course lab description for the rules and semantics;
//                 cppreference.com for math/errno;
// AI (DeepSeek & ChatGPT): Used to discuss the overall program structure and parser design. 
// Prompt: "What is the best way to structure a recursive descent parser and tokenizer in C for arithmetic expressions?"
//   I used the high-level architectural suggestions and wrote the implementation myself.
// Resource (cppreference.com): Used for standard library function signatures (stdio.h, stdlib.h, math.h, ctype.h, dirent.h, sys/stat.h).
// AI (DeepSeek): Prompt "How to fix format truncation warnings in snprintf in C"
//   I used the advice to add precision limits (%.2000s) to the format specifiers to guarantee the compiler that the buffers won't overflow.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// ------------------------------------------------------------------
// Global State
// ------------------------------------------------------------------
static char   *source_data = NULL;   // The entire file loaded in RAM
static size_t  source_size = 0;      // Total bytes in source_data
static size_t  scan_index  = 0;      // Current read cursor (0-based)

static size_t  active_line_begin = 0;
static size_t  active_line_limit = 0; // Points to just after '\n'

// Error tracking (line-level and file-level)
static int     line_had_error = 0;
static size_t  line_err_idx   = 0;

static int     file_had_error = 0;
static size_t  file_err_idx   = 0;

// ------------------------------------------------------------------
// Error Tracking & Position Logic
// ------------------------------------------------------------------
// Why errors are reported at the position chosen:
// 1. Missing operand or missing ')': The error is reported at the line's 
//    newline character. If the file ends without a newline, the spec requires 
//    the position to be file_length + 1.
// 2. Invalid characters, extra operators, division by zero: The error is 
//    reported at the exact character/start of the right operand that caused 
//    the parser to fail.
static void log_error(size_t pos_1_based) {
    if (!line_had_error) {
        line_had_error = 1;
        line_err_idx   = pos_1_based;
    }
    if (!file_had_error) {
        file_had_error = 1;
        file_err_idx   = pos_1_based;
    }
}

static size_t get_line_end_pos(void) {
    if (active_line_limit > 0 && source_data[active_line_limit - 1] == '\n') {
        return active_line_limit;
    }
    return source_size + 1;
}

// ------------------------------------------------------------------
// Tokenizer
// ------------------------------------------------------------------
typedef enum {
    TK_EOF, TK_NUM_INT, TK_NUM_FLT, 
    TK_ADD, TK_SUB, TK_MUL, TK_DIV, TK_POW, 
    TK_OPEN, TK_CLOSE, TK_INVALID
} TokenKind;

typedef struct {
    TokenKind kind;
    long long i_val;
    double    f_val;
    int       is_flt;
    size_t    offset;
} Token;

// ------------------------------------------------------------------
// Tokenizer Position Tracking
// ------------------------------------------------------------------
// Why we track positions this way: 
// The entire file is loaded into a single memory buffer (`source_data`). 
// The tokenizer maintains a 0-based `scan_index` that traverses the buffer.
// When an error is encountered, it logs `scan_index + 1` to comply with the 
// 1-based position rules. This avoids complex line/column calculations.
static void advance_spaces(void) {
    while (scan_index < active_line_limit && 
           (source_data[scan_index] == ' ' || source_data[scan_index] == '\t')) {
        scan_index++;
    }
}

static Token scan_token(void) {
    Token tok;
    memset(&tok, 0, sizeof(tok));

    advance_spaces();
    tok.offset = scan_index;

    if (scan_index >= active_line_limit || source_data[scan_index] == '\n') {
        tok.kind = TK_EOF;
        return tok;
    }

    char ch = source_data[scan_index];

    if (isdigit((unsigned char)ch)) {
        size_t begin = scan_index;
        while (scan_index < active_line_limit && isdigit((unsigned char)source_data[scan_index])) {
            scan_index++;
        }

        int has_decimal = 0;
        if (scan_index < active_line_limit && source_data[scan_index] == '.') {
            size_t dot_loc = scan_index;
            scan_index++;
            if (scan_index >= active_line_limit || !isdigit((unsigned char)source_data[scan_index])) {
                log_error(dot_loc + 1);
                tok.kind = TK_INVALID;
                return tok;
            }
            while (scan_index < active_line_limit && isdigit((unsigned char)source_data[scan_index])) {
                scan_index++;
            }
            has_decimal = 1;
        }

        if (scan_index < active_line_limit && (source_data[scan_index] == 'e' || source_data[scan_index] == 'E')) {
            log_error(scan_index + 1);
            tok.kind = TK_INVALID;
            return tok;
        }

        size_t len = scan_index - begin;
        char *buf = malloc(len + 1);
        if (!buf) { tok.kind = TK_INVALID; return tok; }
        memcpy(buf, source_data + begin, len);
        buf[len] = '\0';

        if (has_decimal) {
            tok.f_val  = strtod(buf, NULL);
            tok.is_flt = 1;
            tok.kind   = TK_NUM_FLT;
        } else {
            tok.i_val  = strtoll(buf, NULL, 10);
            tok.is_flt = 0;
            tok.kind   = TK_NUM_INT;
        }
        free(buf);
        return tok;
    }

    switch (ch) {
        case '+': scan_index++; tok.kind = TK_ADD; return tok;
        case '-': scan_index++; tok.kind = TK_SUB; return tok;
        case '/': scan_index++; tok.kind = TK_DIV; return tok;
        case '(': scan_index++; tok.kind = TK_OPEN; return tok;
        case ')': scan_index++; tok.kind = TK_CLOSE; return tok;
        case '*':
            scan_index++;
            if (scan_index < active_line_limit && source_data[scan_index] == '*') {
                scan_index++;
                tok.kind = TK_POW;
            } else {
                tok.kind = TK_MUL;
            }
            return tok;
        default:
            break;
    }

    log_error(scan_index + 1);
    scan_index++;
    tok.kind = TK_INVALID;
    return tok;
}

static Token peek_token(void) {
    size_t saved_scan = scan_index;
    int saved_line_err = line_had_error;
    size_t saved_line_pos = line_err_idx;
    int saved_file_err = file_had_error;
    size_t saved_file_pos = file_err_idx;

    Token t = scan_token();

    scan_index     = saved_scan;
    line_had_error = saved_line_err;
    line_err_idx   = saved_line_pos;
    file_had_error = saved_file_err;
    file_err_idx   = saved_file_pos;
    return t;
}

// ------------------------------------------------------------------
// Evaluation & Math Types
// ------------------------------------------------------------------
typedef struct {
    int is_flt;
    long long i_val;
    double f_val;
} MathVal;

static MathVal create_int(long long v)   { MathVal r; r.is_flt = 0; r.i_val = v; r.f_val = 0.0; return r; }
static MathVal create_flt(double v)      { MathVal r; r.is_flt = 1; r.f_val = v; r.i_val = 0;   return r; }

static long long fast_pow(long long base, long long exp) {
    long long res = 1, b = base, e = exp;
    while (e > 0) {
        if (e & 1) res *= b;
        e >>= 1;
        if (e) b *= b;
    }
    return res;
}

// ------------------------------------------------------------------
// Parser Grammar & Precedence
// ------------------------------------------------------------------
// Why this grammar gives the right precedence:
// Recursive descent naturally enforces precedence by the depth of the call stack. 
// expr() calls term(), term() calls power(), and power() calls atom().
// Operations higher in the tree (+, -) bind less tightly than those deeper (*, /).
// 
// Why '**' is right-associative:
// In parse_power(), when '**' is encountered, we call parse_power() recursively 
// instead of parse_atom(). This causes the right operand to "grab" the rest of the 
// expression, so 2 ** 3 ** 2 correctly groups as 2 ** (3 ** 2) rather than (2 ** 3) ** 2.
//
// Grammar used:
// expr    := term { ('+' | '-') term }
// term    := power { ('*' | '/') power }
// power   := primary [ '**' power ]
// primary := NUMBER | '(' expr ')'
static MathVal parse_expr(void);
static MathVal parse_term(void);
static MathVal parse_power(void);
static MathVal parse_atom(void);

static void verify_end_of_line(void) {
    Token t = scan_token();
    if (t.kind != TK_EOF) {
        log_error(t.offset + 1);
    }
}

static MathVal parse_atom(void) {
    MathVal res = create_int(0);
    if (line_had_error) return res;

    Token t = scan_token();

    if (t.kind == TK_NUM_INT) return create_int(t.i_val);
    if (t.kind == TK_NUM_FLT) return create_flt(t.f_val);

    if (t.kind == TK_OPEN) {
        MathVal inner = parse_expr();
        if (line_had_error) return res;
        Token close = scan_token();
        
        if (close.kind != TK_CLOSE) {
            log_error(get_line_end_pos());
            return res;
        }
        return inner;
    }

    if (t.kind == TK_EOF) {
        log_error(get_line_end_pos());
        return res;
    }

    if (t.kind == TK_CLOSE) {
        log_error(t.offset + 1);
        return res;
    }

    if (t.kind == TK_INVALID) {
        return res;
    }

    log_error(t.offset + 1);
    return res;
}

static MathVal parse_power(void) {
    MathVal left = parse_atom();
    if (line_had_error) return left;

    Token look = peek_token();
    if (look.kind == TK_POW) {
        (void)scan_token();

        size_t operand_start = scan_index;
        while (operand_start < active_line_limit && 
               (source_data[operand_start] == ' ' || source_data[operand_start] == '\t')) {
            operand_start++;
        }
        size_t op_pos = operand_start + 1;

        MathVal right = parse_power();
        if (line_had_error) return left;

        // How we decide between integer and float arithmetic:
        // If either the base or the exponent is a float, we promote both to doubles 
        // and use the math library's pow(). If both are strictly integers, we maintain 
        // integer math using fast_pow(). This preserves exact integer results and 
        // catches negative integer exponents as errors.
        int flt_mode = left.is_flt || right.is_flt;

        if (!flt_mode) {
            if (right.i_val < 0) {
                log_error(op_pos);
                return left;
            }
            return create_int(fast_pow(left.i_val, right.i_val));
        } else {
            double b = left.is_flt ? left.f_val : (double)left.i_val;
            double e = right.is_flt ? right.f_val : (double)right.i_val;
            double val = pow(b, e);
            
            if (isinf(val) || isnan(val)) {
                log_error(look.offset + 1);
                return left;
            }
            return create_flt(val);
        }
    }
    return left;
}

static MathVal parse_term(void) {
    MathVal left = parse_power();
    if (line_had_error) return left;

    while (1) {
        Token look = peek_token();
        if (look.kind != TK_MUL && look.kind != TK_DIV) break;

        TokenKind op = look.kind;
        (void)scan_token();

        size_t operand_start = scan_index;
        while (operand_start < active_line_limit && 
               (source_data[operand_start] == ' ' || source_data[operand_start] == '\t')) {
            operand_start++;
        }
        size_t op_pos = operand_start + 1;

        MathVal right = parse_power();
        if (line_had_error) return left;

        int flt_mode = left.is_flt || right.is_flt;

        if (op == TK_MUL) {
            if (!flt_mode) {
                left = create_int(left.i_val * right.i_val);
            } else {
                double a = left.is_flt ? left.f_val : (double)left.i_val;
                double b = right.is_flt ? right.f_val : (double)right.i_val;
                left = create_flt(a * b);
            }
        } else {
            if (!flt_mode) {
                if (right.i_val == 0) {
                    log_error(op_pos);
                    return left;
                }
                left = create_int(left.i_val / right.i_val);
            } else {
                double a = left.is_flt ? left.f_val : (double)left.i_val;
                double b = right.is_flt ? right.f_val : (double)right.i_val;
                if (b == 0.0) {
                    log_error(op_pos);
                    return left;
                }
                left = create_flt(a / b);
            }
        }
    }
    return left;
}

static MathVal parse_expr(void) {
    MathVal left = parse_term();
    if (line_had_error) return left;

    while (1) {
        Token look = peek_token();
        if (look.kind != TK_ADD && look.kind != TK_SUB) break;

        TokenKind op = look.kind;
        (void)scan_token();

        MathVal right = parse_term();
        if (line_had_error) return left;

        int flt_mode = left.is_flt || right.is_flt;
        if (!flt_mode) {
            left = create_int(op == TK_ADD ? left.i_val + right.i_val : left.i_val - right.i_val);
        } else {
            double a = left.is_flt ? left.f_val : (double)left.i_val;
            double b = right.is_flt ? right.f_val : (double)right.i_val;
            left = create_flt(op == TK_ADD ? a + b : a - b);
        }
    }
    return left;
}

// ------------------------------------------------------------------
// Large File Processing
// ------------------------------------------------------------------
// How we handle large files without slowing down:
// We allocate a single buffer matching the exact file size and read the entire 
// file in one fread() operation. The parser then traverses this continuous buffer 
// in O(n) time. There are no repeated string copies or line-by-line reallocations.
static int load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    source_data = malloc((size_t)sz + 1);
    if (!source_data) { fclose(f); return -1; }
    size_t read_count = fread(source_data, 1, (size_t)sz, f);
    fclose(f);
    source_data[read_count] = '\0';
    source_size = read_count;
    return 0;
}

static int process_source(const char *in_path, const char *out_path) {
    if (load_file(in_path) != 0) {
        fprintf(stderr, "Error: cannot read input file '%s'\n", in_path);
        return 1;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot write output file '%s'\n", out_path);
        free(source_data); source_data = NULL; source_size = 0;
        return 1;
    }

    size_t out_cap = 1024, out_cnt = 0;
    MathVal *saved_results = malloc(out_cap * sizeof(MathVal));
    file_had_error = 0;

    size_t idx = 0;
    while (idx < source_size) {
        size_t line_beg = idx;
        size_t line_end = idx;
        while (line_end < source_size && source_data[line_end] != '\n') line_end++;

        size_t skip = line_beg;
        while (skip < line_end && (source_data[skip] == ' ' || source_data[skip] == '\t')) skip++;

        if (skip == line_end || source_data[skip] == '#') {
            idx = (line_end < source_size) ? line_end + 1 : line_end;
            continue;
        }

        active_line_begin = line_beg;
        active_line_limit = (line_end < source_size) ? (line_end + 1) : line_end;
        scan_index = line_beg;
        line_had_error = 0;

        MathVal val = parse_expr();
        if (!line_had_error) verify_end_of_line();

        if (line_had_error) {
            break;
        } else {
            if (out_cnt >= out_cap) {
                out_cap *= 2;
                saved_results = realloc(saved_results, out_cap * sizeof(MathVal));
            }
            saved_results[out_cnt++] = val;
        }

        idx = (line_end < source_size) ? line_end + 1 : line_end;
    }

    if (file_had_error) {
        fprintf(out, "ERROR:%zu\n", file_err_idx);
    } else {
        for (size_t i = 0; i < out_cnt; i++) {
            if (saved_results[i].is_flt) fprintf(out, "%.15g\n", saved_results[i].f_val);
            else                         fprintf(out, "%.15g\n", (double)saved_results[i].i_val);
        }
    }

    free(saved_results);
    fclose(out);
    free(source_data); source_data = NULL; source_size = 0;
    return 0;
}

// ------------------------------------------------------------------
// Filenames and CLI
// ------------------------------------------------------------------
static const char *MY_NAME     = "Joyal";
static const char *MY_LASTNAME = "Joshy";
static const char *MY_ID       = "251ADB018";

static void make_lowercase(const char *src, char *dst, size_t max) {
    size_t i = 0;
    for (; src[i] && i + 1 < max; i++) dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

static void extract_path_parts(const char *full, char *dir, size_t dir_sz, char *name, size_t name_sz) {
    const char *last_slash = strrchr(full, '/');
    if (last_slash) {
        size_t d_len = (size_t)(last_slash - full);
        if (d_len >= dir_sz) d_len = dir_sz - 1;
        memcpy(dir, full, d_len); dir[d_len] = '\0';
        strncpy(name, last_slash + 1, name_sz - 1); name[name_sz - 1] = '\0';
    } else {
        dir[0] = '\0';
        strncpy(name, full, name_sz - 1); name[name_sz - 1] = '\0';
    }
    char *ext = strrchr(name, '.');
    if (ext && strcmp(ext, ".txt") == 0) *ext = '\0';
}

static int make_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    return mkdir(path, 0777) == 0 ? 0 : -1;
}

static int run_single(const char *in_file, const char *out_folder) {
    char dir[1024], base[1024];
    extract_path_parts(in_file, dir, sizeof dir, base, sizeof base);

    char ln[128], lln[128];
    make_lowercase(MY_NAME,     ln, sizeof ln);
    make_lowercase(MY_LASTNAME, lln, sizeof lln);

    char final_name[2048];
    snprintf(final_name, sizeof final_name, "%s_%s_%s_%s.txt", base, ln, lln, MY_ID);

    char final_path[4096];
    if (out_folder[0] == '\0') snprintf(final_path, sizeof final_path, "%s", final_name);
    else                       snprintf(final_path, sizeof final_path, "%s/%s", out_folder, final_name);

    return process_source(in_file, final_path);
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n", prog);
}

int main(int argc, char **argv) {
    const char *target_dir = NULL, *out_dir = NULL, *in_file = NULL;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "-d") || !strcmp(arg, "--dir")) {
            if (++i >= argc) { print_usage(argv[0]); return 1; }
            target_dir = argv[i];
        } else if (!strcmp(arg, "-o") || !strcmp(arg, "--output-dir")) {
            if (++i >= argc) { print_usage(argv[0]); return 1; }
            out_dir = argv[i];
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            print_usage(argv[0]); return 1;
        } else {
            if (in_file) { fprintf(stderr, "Multiple inputs specified\n"); return 1; }
            in_file = arg;
        }
    }

    if (target_dir && in_file) { fprintf(stderr, "Cannot use -d with an input file\n"); return 1; }
    if (!target_dir && !in_file) { print_usage(argv[0]); return 1; }

    char fallback_dir[2048];
    const char *write_to = out_dir;

    if (!write_to) {
        char ln[128], lln[128];
        make_lowercase(MY_NAME,     ln, sizeof ln);
        make_lowercase(MY_LASTNAME, lln, sizeof lln);

        if (in_file) {
            char d[1024], b[1024];
            extract_path_parts(in_file, d, sizeof d, b, sizeof b);
            snprintf(fallback_dir, sizeof fallback_dir, "%s_%s_%s_%s", b, ln, lln, MY_ID);
        } else {
            char tmp[1024];
            strncpy(tmp, target_dir, sizeof tmp - 1); tmp[sizeof tmp - 1] = '\0';
            size_t len = strlen(tmp);
            while (len > 0 && tmp[len - 1] == '/') tmp[--len] = '\0';
            const char *last_slash = strrchr(tmp, '/');
            const char *b = last_slash ? last_slash + 1 : tmp;
            snprintf(fallback_dir, sizeof fallback_dir, "%s_%s_%s_%s", b, ln, lln, MY_ID);
        }
        write_to = fallback_dir;
    }

    if (make_directory(write_to) != 0) {
        fprintf(stderr, "Error: cannot create output directory '%s'\n", write_to);
        return 1;
    }

    if (in_file) return run_single(in_file, write_to);

    DIR *d = opendir(target_dir);
    if (!d) { fprintf(stderr, "Error: cannot open directory '%s'\n", target_dir); return 1; }
    
    int exit_status = 0;
    struct dirent *entry;
    while ((entry = readdir(d))) {
        const char *fname = entry->d_name;
        if (!strcmp(fname, ".") || !strcmp(fname, "..")) continue;
        size_t f_len = strlen(fname);
        if (f_len < 4 || strcmp(fname + f_len - 4, ".txt") != 0) continue;

        char full_path[4096];
        snprintf(full_path, sizeof full_path, "%s/%s", target_dir, fname);
        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (run_single(full_path, write_to) != 0) exit_status = 1;
    }
    closedir(d);
    return exit_status;
}