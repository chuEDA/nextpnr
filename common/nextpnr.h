/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <algorithm>
#include <assert.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/adaptor/reversed.hpp>
#ifndef NPNR_DISABLE_THREADS
#include <boost/thread.hpp>
#endif

#ifndef NEXTPNR_H
#define NEXTPNR_H

#ifdef NEXTPNR_NAMESPACE
#define NEXTPNR_NAMESPACE_PREFIX NEXTPNR_NAMESPACE::
#define NEXTPNR_NAMESPACE_BEGIN namespace NEXTPNR_NAMESPACE {
#define NEXTPNR_NAMESPACE_END }
#define USING_NEXTPNR_NAMESPACE using namespace NEXTPNR_NAMESPACE;
#else
#define NEXTPNR_NAMESPACE_PREFIX
#define NEXTPNR_NAMESPACE_BEGIN
#define NEXTPNR_NAMESPACE_END
#define USING_NEXTPNR_NAMESPACE
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NPNR_ATTRIBUTE(...) __attribute__((__VA_ARGS__))
#define NPNR_NORETURN __attribute__((noreturn))
#define NPNR_DEPRECATED __attribute__((deprecated))
#define NPNR_PACKED_STRUCT(...) __VA_ARGS__ __attribute__((packed))
#elif defined(_MSC_VER)
#define NPNR_ATTRIBUTE(...)
#define NPNR_NORETURN __declspec(noreturn)
#define NPNR_DEPRECATED __declspec(deprecated)
#define NPNR_PACKED_STRUCT(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#else
#define NPNR_ATTRIBUTE(...)
#define NPNR_NORETURN
#define NPNR_DEPRECATED
#define NPNR_PACKED_STRUCT(...) __VA_ARGS__
#endif

NEXTPNR_NAMESPACE_BEGIN

class assertion_failure : public std::runtime_error
{
  public:
    assertion_failure(std::string msg, std::string expr_str, std::string filename, int line);

    std::string msg;
    std::string expr_str;
    std::string filename;
    int line;
};

NPNR_NORETURN
inline void assert_fail_impl(const char *message, const char *expr_str, const char *filename, int line)
{
    throw assertion_failure(message, expr_str, filename, line);
}

NPNR_NORETURN
inline void assert_fail_impl_str(std::string message, const char *expr_str, const char *filename, int line)
{
    throw assertion_failure(message, expr_str, filename, line);
}

#define NPNR_ASSERT(cond) (!(cond) ? assert_fail_impl(#cond, #cond, __FILE__, __LINE__) : (void)true)
#define NPNR_ASSERT_MSG(cond, msg) (!(cond) ? assert_fail_impl(msg, #cond, __FILE__, __LINE__) : (void)true)
#define NPNR_ASSERT_FALSE(msg) (assert_fail_impl(msg, "false", __FILE__, __LINE__))
#define NPNR_ASSERT_FALSE_STR(msg) (assert_fail_impl_str(msg, "false", __FILE__, __LINE__))

#define STRINGIFY(x) #x

struct BaseCtx;
struct Context;

struct IdString
{
    int index;

    static void initialize_arch(const BaseCtx *ctx);

    static void initialize_add(const BaseCtx *ctx, const char *s, int idx);

    constexpr IdString() : index(0) {}
    explicit constexpr IdString(int index) : index(index) {}

    void set(const BaseCtx *ctx, const std::string &s);

    IdString(const BaseCtx *ctx, const std::string &s) { set(ctx, s); }

    IdString(const BaseCtx *ctx, const char *s) { set(ctx, s); }

    const std::string &str(const BaseCtx *ctx) const;

    const char *c_str(const BaseCtx *ctx) const;

    bool operator<(const IdString &other) const { return index < other.index; }

    bool operator==(const IdString &other) const { return index == other.index; }

    bool operator!=(const IdString &other) const { return index != other.index; }

    bool empty() const { return index == 0; }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX IdString>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX IdString &obj) const noexcept
    {
        return std::hash<int>()(obj.index);
    }
};
} // namespace std

NEXTPNR_NAMESPACE_BEGIN

// An small size optimised array that is statically allocated when the size is N or less; heap allocated otherwise
template <typename T, size_t N> class SSOArray
{
  private:
    union
    {
        T data_static[N];
        T *data_heap;
    };
    size_t m_size;
    inline bool is_heap() const { return (m_size > N); }
    void alloc()
    {
        if (is_heap()) {
            data_heap = new T[m_size];
        }
    }

  public:
    T *data() { return is_heap() ? data_heap : data_static; }
    const T *data() const { return is_heap() ? data_heap : data_static; }
    size_t size() const { return m_size; }

    T *begin() { return data(); }
    T *end() { return data() + m_size; }
    const T *begin() const { return data(); }
    const T *end() const { return data() + m_size; }

    SSOArray() : m_size(0){};

    SSOArray(size_t size, const T &init = T()) : m_size(size)
    {
        alloc();
        std::fill(begin(), end(), init);
    }

    SSOArray(const SSOArray &other) : m_size(other.size())
    {
        alloc();
        std::copy(other.begin(), other.end(), begin());
    }

    template <typename Tother> SSOArray(const Tother &other) : m_size(other.size())
    {
        alloc();
        std::copy(other.begin(), other.end(), begin());
    }

    ~SSOArray()
    {
        if (is_heap()) {
            delete[] data_heap;
        }
    }

    bool operator==(const SSOArray &other) const
    {
        if (size() != other.size())
            return false;
        return std::equal(begin(), end(), other.begin());
    }
    bool operator!=(const SSOArray &other) const
    {
        if (size() != other.size())
            return true;
        return !std::equal(begin(), end(), other.begin());
    }
    T &operator[](size_t idx)
    {
        NPNR_ASSERT(idx < m_size);
        return data()[idx];
    }
    const T &operator[](size_t idx) const
    {
        NPNR_ASSERT(idx < m_size);
        return data()[idx];
    }
};

struct IdStringList
{
    SSOArray<IdString, 4> ids;

    IdStringList(){};
    explicit IdStringList(size_t n) : ids(n, IdString()){};
    explicit IdStringList(IdString id) : ids(1, id){};
    template <typename Tlist> explicit IdStringList(const Tlist &list) : ids(list){};

    static IdStringList parse(Context *ctx, const std::string &str);
    void build_str(const Context *ctx, std::string &str) const;
    std::string str(const Context *ctx) const;

    size_t size() const { return ids.size(); }
    const IdString *begin() const { return ids.begin(); }
    const IdString *end() const { return ids.end(); }
    const IdString &operator[](size_t idx) const { return ids[idx]; }
    bool operator==(const IdStringList &other) const { return ids == other.ids; }
    bool operator!=(const IdStringList &other) const { return ids != other.ids; }
    bool operator<(const IdStringList &other) const
    {
        if (size() > other.size())
            return false;
        if (size() < other.size())
            return true;
        for (size_t i = 0; i < size(); i++) {
            IdString a = ids[i], b = other[i];
            if (a.index < b.index)
                return true;
            if (a.index > b.index)
                return false;
        }
        return false;
    }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX IdStringList>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX IdStringList &obj) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<size_t>()(obj.size()));
        for (auto &id : obj)
            boost::hash_combine(seed, hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(id));
        return seed;
    }
};
} // namespace std

NEXTPNR_NAMESPACE_BEGIN

// A ring buffer of strings, so we can return a simple const char * pointer for %s formatting - inspired by how logging
// in Yosys works Let's just hope noone tries to log more than 100 things in one call....
class StrRingBuffer
{
  private:
    static const size_t N = 100;
    std::array<std::string, N> buffer;
    size_t index = 0;

  public:
    std::string &next();
};

struct GraphicElement
{
    enum type_t
    {
        TYPE_NONE,
        TYPE_LINE,
        TYPE_ARROW,
        TYPE_BOX,
        TYPE_CIRCLE,
        TYPE_LABEL,

        TYPE_MAX
    } type = TYPE_NONE;

    enum style_t
    {
        STYLE_GRID,
        STYLE_FRAME,    // Static "frame". Contrast between STYLE_INACTIVE and STYLE_ACTIVE
        STYLE_HIDDEN,   // Only display when object is selected or highlighted
        STYLE_INACTIVE, // Render using low-contrast color
        STYLE_ACTIVE,   // Render using high-contast color

        // UI highlight groups
        STYLE_HIGHLIGHTED0,
        STYLE_HIGHLIGHTED1,
        STYLE_HIGHLIGHTED2,
        STYLE_HIGHLIGHTED3,
        STYLE_HIGHLIGHTED4,
        STYLE_HIGHLIGHTED5,
        STYLE_HIGHLIGHTED6,
        STYLE_HIGHLIGHTED7,

        STYLE_SELECTED,
        STYLE_HOVER,

        STYLE_MAX
    } style = STYLE_FRAME;

    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, z = 0;
    std::string text;
    GraphicElement(){};
    GraphicElement(type_t type, style_t style, float x1, float y1, float x2, float y2, float z)
            : type(type), style(style), x1(x1), y1(y1), x2(x2), y2(y2), z(z){};
};

struct Loc
{
    int x = -1, y = -1, z = -1;

    Loc() {}
    Loc(int x, int y, int z) : x(x), y(y), z(z) {}

    bool operator==(const Loc &other) const { return (x == other.x) && (y == other.y) && (z == other.z); }
    bool operator!=(const Loc &other) const { return (x != other.x) || (y != other.y) || (z != other.z); }
};

struct ArcBounds
{
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1;

    ArcBounds() {}
    ArcBounds(int x0, int y0, int x1, int y1) : x0(x0), y0(y0), x1(x1), y1(y1){};

    int distance(Loc loc) const
    {
        int dist = 0;
        if (loc.x < x0)
            dist += x0 - loc.x;
        if (loc.x > x1)
            dist += loc.x - x1;
        if (loc.y < y0)
            dist += y0 - loc.y;
        if (loc.y > y1)
            dist += loc.y - y1;
        return dist;
    };
};

struct TimingConstrObjectId
{
    int32_t index = -1;

    bool operator==(const TimingConstrObjectId &other) const { return index == other.index; }
    bool operator!=(const TimingConstrObjectId &other) const { return index != other.index; }
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX Loc>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX Loc &obj) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(obj.x));
        boost::hash_combine(seed, hash<int>()(obj.y));
        boost::hash_combine(seed, hash<int>()(obj.z));
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX TimingConstrObjectId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX TimingConstrObjectId &obj) const noexcept
    {
        return hash<int>()(obj.index);
    }
};

} // namespace std

#include "archdefs.h"

NEXTPNR_NAMESPACE_BEGIN

struct DecalXY
{
    DecalId decal;
    float x = 0, y = 0;

    bool operator==(const DecalXY &other) const { return (decal == other.decal && x == other.x && y == other.y); }
};

struct BelPin
{
    BelId bel;
    IdString pin;
};

struct CellInfo;

struct Region
{
    IdString name;

    bool constr_bels = false;
    bool constr_wires = false;
    bool constr_pips = false;

    std::unordered_set<BelId> bels;
    std::unordered_set<WireId> wires;
    std::unordered_set<Loc> piplocs;
};

enum PlaceStrength
{
    STRENGTH_NONE = 0,
    STRENGTH_WEAK = 1,
    STRENGTH_STRONG = 2,
    STRENGTH_FIXED = 3,
    STRENGTH_LOCKED = 4,
    STRENGTH_USER = 5
};

struct PortRef
{
    CellInfo *cell = nullptr;
    IdString port;
    delay_t budget = 0;
};

struct PipMap
{
    PipId pip = PipId();
    PlaceStrength strength = STRENGTH_NONE;
};

struct Property
{
    enum State : char
    {
        S0 = '0',
        S1 = '1',
        Sx = 'x',
        Sz = 'z'
    };

    Property();
    Property(int64_t intval, int width = 32);
    Property(const std::string &strval);
    Property(State bit);
    Property &operator=(const Property &other) = default;

    bool is_string;

    // The string literal (for string values), or a string of [01xz] (for numeric values)
    std::string str;
    // The lower 64 bits (for numeric values), unused for string values
    int64_t intval;

    void update_intval()
    {
        intval = 0;
        for (int i = 0; i < int(str.size()); i++) {
            NPNR_ASSERT(str[i] == S0 || str[i] == S1 || str[i] == Sx || str[i] == Sz);
            if ((str[i] == S1) && i < 64)
                intval |= (1ULL << i);
        }
    }

    int64_t as_int64() const
    {
        NPNR_ASSERT(!is_string);
        return intval;
    }
    std::vector<bool> as_bits() const
    {
        std::vector<bool> result;
        result.reserve(str.size());
        NPNR_ASSERT(!is_string);
        for (auto c : str)
            result.push_back(c == S1);
        return result;
    }
    std::string as_string() const
    {
        NPNR_ASSERT(is_string);
        return str;
    }
    const char *c_str() const
    {
        NPNR_ASSERT(is_string);
        return str.c_str();
    }
    size_t size() const { return is_string ? 8 * str.size() : str.size(); }
    double as_double() const
    {
        NPNR_ASSERT(is_string);
        return std::stod(str);
    }
    bool as_bool() const
    {
        if (int(str.size()) <= 64)
            return intval != 0;
        else
            return std::any_of(str.begin(), str.end(), [](char c) { return c == S1; });
    }
    bool is_fully_def() const
    {
        return !is_string && std::all_of(str.begin(), str.end(), [](char c) { return c == S0 || c == S1; });
    }
    Property extract(int offset, int len, State padding = State::S0) const
    {
        Property ret;
        ret.is_string = false;
        ret.str.reserve(len);
        for (int i = offset; i < offset + len; i++)
            ret.str.push_back(i < int(str.size()) ? str[i] : padding);
        ret.update_intval();
        return ret;
    }
    // Convert to a string representation, escaping literal strings matching /^[01xz]* *$/ by adding a space at the end,
    // to disambiguate from binary strings
    std::string to_string() const;
    // Convert a string of four-value binary [01xz], or a literal string escaped according to the above rule
    // to a Property
    static Property from_string(const std::string &s);
};

inline bool operator==(const Property &a, const Property &b) { return a.is_string == b.is_string && a.str == b.str; }
inline bool operator!=(const Property &a, const Property &b) { return a.is_string != b.is_string || a.str != b.str; }

struct ClockConstraint;

struct NetInfo : ArchNetInfo
{
    IdString name, hierpath;
    int32_t udata = 0;

    PortRef driver;
    std::vector<PortRef> users;
    std::unordered_map<IdString, Property> attrs;

    // wire -> uphill_pip
    std::unordered_map<WireId, PipMap> wires;

    std::vector<IdString> aliases; // entries in net_aliases that point to this net

    std::unique_ptr<ClockConstraint> clkconstr;

    TimingConstrObjectId tmg_id;

    Region *region = nullptr;
};

enum PortType
{
    PORT_IN = 0,
    PORT_OUT = 1,
    PORT_INOUT = 2
};

struct PortInfo
{
    IdString name;
    NetInfo *net;
    PortType type;
    TimingConstrObjectId tmg_id;
};

struct CellInfo : ArchCellInfo
{
    IdString name, type, hierpath;
    int32_t udata;

    std::unordered_map<IdString, PortInfo> ports;
    std::unordered_map<IdString, Property> attrs, params;

    BelId bel;
    PlaceStrength belStrength = STRENGTH_NONE;

    // cell_port -> bel_pin
    std::unordered_map<IdString, IdString> pins;

    // placement constraints
    CellInfo *constr_parent = nullptr;
    std::vector<CellInfo *> constr_children;
    const int UNCONSTR = INT_MIN;
    int constr_x = UNCONSTR;   // this.x - parent.x
    int constr_y = UNCONSTR;   // this.y - parent.y
    int constr_z = UNCONSTR;   // this.z - parent.z
    bool constr_abs_z = false; // parent.z := 0
    // parent.[xyz] := 0 when (constr_parent == nullptr)

    Region *region = nullptr;
    TimingConstrObjectId tmg_id;

    void addInput(IdString name);
    void addOutput(IdString name);
    void addInout(IdString name);

    void setParam(IdString name, Property value);
    void unsetParam(IdString name);
    void setAttr(IdString name, Property value);
    void unsetAttr(IdString name);
};

enum TimingPortClass
{
    TMG_CLOCK_INPUT,     // Clock input to a sequential cell
    TMG_GEN_CLOCK,       // Generated clock output (PLL, DCC, etc)
    TMG_REGISTER_INPUT,  // Input to a register, with an associated clock (may also have comb. fanout too)
    TMG_REGISTER_OUTPUT, // Output from a register
    TMG_COMB_INPUT,      // Combinational input, no paths end here
    TMG_COMB_OUTPUT,     // Combinational output, no paths start here
    TMG_STARTPOINT,      // Unclocked primary startpoint, such as an IO cell output
    TMG_ENDPOINT,        // Unclocked primary endpoint, such as an IO cell input
    TMG_IGNORE,          // Asynchronous to all clocks, "don't care", and should be ignored (false path) for analysis
};

enum ClockEdge
{
    RISING_EDGE,
    FALLING_EDGE
};

struct TimingClockingInfo
{
    IdString clock_port; // Port name of clock domain
    ClockEdge edge;
    DelayInfo setup, hold; // Input timing checks
    DelayInfo clockToQ;    // Output clock-to-Q time
};

struct ClockConstraint
{
    DelayInfo high;
    DelayInfo low;
    DelayInfo period;

    TimingConstrObjectId domain_tmg_id;
};

struct TimingConstraintObject
{
    TimingConstrObjectId id;
    enum
    {
        ANYTHING,
        CLOCK_DOMAIN,
        NET,
        CELL,
        CELL_PORT
    } type;
    IdString entity; // Name of clock net; net or cell
    IdString port;   // Name of port on a cell
};

struct TimingConstraint
{
    IdString name;

    enum
    {
        FALSE_PATH,
        MIN_DELAY,
        MAX_DELAY,
        MULTICYCLE,
    } type;

    delay_t value;

    std::unordered_set<TimingConstrObjectId> from;
    std::unordered_set<TimingConstrObjectId> to;
};

// Represents the contents of a non-leaf cell in a design
// with hierarchy

struct HierarchicalPort
{
    IdString name;
    PortType dir;
    std::vector<IdString> nets;
    int offset;
    bool upto;
};

struct HierarchicalCell
{
    IdString name, type, parent, fullpath;
    // Name inside cell instance -> global name
    std::unordered_map<IdString, IdString> leaf_cells, nets;
    // Global name -> name inside cell instance
    std::unordered_map<IdString, IdString> leaf_cells_by_gname, nets_by_gname;
    // Cell port to net
    std::unordered_map<IdString, HierarchicalPort> ports;
    // Name inside cell instance -> global name
    std::unordered_map<IdString, IdString> hier_cells;
};

inline bool operator==(const std::pair<const TimingConstrObjectId, TimingConstraint *> &a,
                       const std::pair<TimingConstrObjectId, TimingConstraint *> &b)
{
    return a.first == b.first && a.second == b.second;
}

struct DeterministicRNG
{
    uint64_t rngstate;

    DeterministicRNG() : rngstate(0x3141592653589793) {}

    uint64_t rng64()
    {
        // xorshift64star
        // https://arxiv.org/abs/1402.6246

        uint64_t retval = rngstate * 0x2545F4914F6CDD1D;

        rngstate ^= rngstate >> 12;
        rngstate ^= rngstate << 25;
        rngstate ^= rngstate >> 27;

        return retval;
    }

    int rng() { return rng64() & 0x3fffffff; }

    int rng(int n)
    {
        assert(n > 0);

        // round up to power of 2
        int m = n - 1;
        m |= (m >> 1);
        m |= (m >> 2);
        m |= (m >> 4);
        m |= (m >> 8);
        m |= (m >> 16);
        m += 1;

        while (1) {
            int x = rng64() & (m - 1);
            if (x < n)
                return x;
        }
    }

    void rngseed(uint64_t seed)
    {
        rngstate = seed ? seed : 0x3141592653589793;
        for (int i = 0; i < 5; i++)
            rng64();
    }

    template <typename Iter> void shuffle(const Iter &begin, const Iter &end)
    {
        size_t size = end - begin;
        for (size_t i = 0; i != size; i++) {
            size_t j = i + rng(size - i);
            if (j > i)
                std::swap(*(begin + i), *(begin + j));
        }
    }

    template <typename T> void shuffle(std::vector<T> &a) { shuffle(a.begin(), a.end()); }

    template <typename T> void sorted_shuffle(std::vector<T> &a)
    {
        std::sort(a.begin(), a.end());
        shuffle(a);
    }
};

struct BaseCtx
{
#ifndef NPNR_DISABLE_THREADS
    // Lock to perform mutating actions on the Context.
    std::mutex mutex;
    boost::thread::id mutex_owner;

    // Lock to be taken by UI when wanting to access context - the yield()
    // method will lock/unlock it when its' released the main mutex to make
    // sure the UI is not starved.
    std::mutex ui_mutex;
#endif

    // ID String database.
    mutable std::unordered_map<std::string, int> *idstring_str_to_idx;
    mutable std::vector<const std::string *> *idstring_idx_to_str;

    // Temporary string backing store for logging
    mutable StrRingBuffer log_strs;

    // Project settings and config switches
    std::unordered_map<IdString, Property> settings;

    // Placed nets and cells.
    std::unordered_map<IdString, std::unique_ptr<NetInfo>> nets;
    std::unordered_map<IdString, std::unique_ptr<CellInfo>> cells;

    // Hierarchical (non-leaf) cells by full path
    std::unordered_map<IdString, HierarchicalCell> hierarchy;
    // This is the root of the above structure
    IdString top_module;

    // Aliases for nets, which may have more than one name due to assignments and hierarchy
    std::unordered_map<IdString, IdString> net_aliases;

    // Top-level ports
    std::unordered_map<IdString, PortInfo> ports;

    // Floorplanning regions
    std::unordered_map<IdString, std::unique_ptr<Region>> region;

    // Context meta data
    std::unordered_map<IdString, Property> attrs;

    Context *as_ctx = nullptr;

    BaseCtx()
    {
        idstring_str_to_idx = new std::unordered_map<std::string, int>;
        idstring_idx_to_str = new std::vector<const std::string *>;
        IdString::initialize_add(this, "", 0);
        IdString::initialize_arch(this);

        TimingConstraintObject wildcard;
        wildcard.id.index = 0;
        wildcard.type = TimingConstraintObject::ANYTHING;
        constraintObjects.push_back(wildcard);
    }

    ~BaseCtx()
    {
        delete idstring_str_to_idx;
        delete idstring_idx_to_str;
    }

    // Must be called before performing any mutating changes on the Ctx/Arch.
    void lock(void)
    {
#ifndef NPNR_DISABLE_THREADS
        mutex.lock();
        mutex_owner = boost::this_thread::get_id();
#endif
    }

    void unlock(void)
    {
#ifndef NPNR_DISABLE_THREADS
        NPNR_ASSERT(boost::this_thread::get_id() == mutex_owner);
        mutex.unlock();
#endif
    }

    // Must be called by the UI before rendering data. This lock will be
    // prioritized when processing code calls yield().
    void lock_ui(void)
    {
#ifndef NPNR_DISABLE_THREADS
        ui_mutex.lock();
        mutex.lock();
#endif
    }

    void unlock_ui(void)
    {
#ifndef NPNR_DISABLE_THREADS
        mutex.unlock();
        ui_mutex.unlock();
#endif
    }

    // Yield to UI by unlocking the main mutex, flashing the UI mutex and
    // relocking the main mutex. Call this when you're performing a
    // long-standing action while holding a lock to let the UI show
    // visualization updates.
    // Must be called with the main lock taken.
    void yield(void)
    {
#ifndef NPNR_DISABLE_THREADS
        unlock();
        ui_mutex.lock();
        ui_mutex.unlock();
        lock();
#endif
    }

    IdString id(const std::string &s) const { return IdString(this, s); }

    IdString id(const char *s) const { return IdString(this, s); }

    Context *getCtx() { return as_ctx; }

    const Context *getCtx() const { return as_ctx; }

    const char *nameOf(IdString name) const { return name.c_str(this); }

    template <typename T> const char *nameOf(const T *obj) const
    {
        if (obj == nullptr)
            return "";
        return obj->name.c_str(this);
    }

    const char *nameOfBel(BelId bel) const;
    const char *nameOfWire(WireId wire) const;
    const char *nameOfPip(PipId pip) const;
    const char *nameOfGroup(GroupId group) const;

    // Wrappers of arch functions that take a string and handle IdStringList parsing
    BelId getBelByNameStr(const std::string &str);
    WireId getWireByNameStr(const std::string &str);
    PipId getPipByNameStr(const std::string &str);
    GroupId getGroupByNameStr(const std::string &str);

    // --------------------------------------------------------------

    bool allUiReload = true;
    bool frameUiReload = false;
    std::unordered_set<BelId> belUiReload;
    std::unordered_set<WireId> wireUiReload;
    std::unordered_set<PipId> pipUiReload;
    std::unordered_set<GroupId> groupUiReload;

    void refreshUi() { allUiReload = true; }

    void refreshUiFrame() { frameUiReload = true; }

    void refreshUiBel(BelId bel) { belUiReload.insert(bel); }

    void refreshUiWire(WireId wire) { wireUiReload.insert(wire); }

    void refreshUiPip(PipId pip) { pipUiReload.insert(pip); }

    void refreshUiGroup(GroupId group) { groupUiReload.insert(group); }

    // --------------------------------------------------------------

    // Timing Constraint API

    // constraint name -> constraint
    std::unordered_map<IdString, std::unique_ptr<TimingConstraint>> constraints;
    // object ID -> object
    std::vector<TimingConstraintObject> constraintObjects;
    // object ID -> constraint
    std::unordered_multimap<TimingConstrObjectId, TimingConstraint *> constrsFrom;
    std::unordered_multimap<TimingConstrObjectId, TimingConstraint *> constrsTo;

    TimingConstrObjectId timingWildcardObject();
    TimingConstrObjectId timingClockDomainObject(NetInfo *clockDomain);
    TimingConstrObjectId timingNetObject(NetInfo *net);
    TimingConstrObjectId timingCellObject(CellInfo *cell);
    TimingConstrObjectId timingPortObject(CellInfo *cell, IdString port);

    NetInfo *getNetByAlias(IdString alias) const
    {
        return nets.count(alias) ? nets.at(alias).get() : nets.at(net_aliases.at(alias)).get();
    }

    void addConstraint(std::unique_ptr<TimingConstraint> constr);
    void removeConstraint(IdString constrName);

    // Intended to simplify Python API
    void addClock(IdString net, float freq);
    void createRectangularRegion(IdString name, int x0, int y0, int x1, int y1);
    void addBelToRegion(IdString name, BelId bel);
    void constrainCellToRegion(IdString cell, IdString region_name);

    // Helper functions for Python bindings
    NetInfo *createNet(IdString name);
    void connectPort(IdString net, IdString cell, IdString port);
    void disconnectPort(IdString cell, IdString port);
    void ripupNet(IdString name);
    void lockNetRouting(IdString name);

    CellInfo *createCell(IdString name, IdString type);
    void copyBelPorts(IdString cell, BelId bel);

    // Workaround for lack of wrappable constructors
    DecalXY constructDecalXY(DecalId decal, float x, float y);

    void archInfoToAttributes();
    void attributesToArchInfo();
};

// For several functions; such as bel/wire/pip attributes; the trivial implementation is to return an empty vector
// But an arch might want to do something fancy with a custom range type that doesn't provide a constructor
// So some cursed C++ is needed to return an empty object if possible; or error out if not; is needed
template <typename Tc> typename std::enable_if<std::is_constructible<Tc>::value, Tc>::type empty_if_possible()
{
    return Tc();
}
template <typename Tc> typename std::enable_if<!std::is_constructible<Tc>::value, Tc>::type empty_if_possible()
{
    NPNR_ASSERT_FALSE("attempting to use default implementation of range-returning function with range type lacking "
                      "default constructor!");
}

template <typename R> struct ArchBase : BaseCtx
{
    // --------------------------------------------------------------
    // Arch API base

    // Basic config
    virtual std::string getChipName() const = 0;
    virtual IdString archId() const { return id(STRINGIFY(ARCHNAME)); }
    virtual int getGridDimX() const = 0;
    virtual int getGridDimY() const = 0;
    virtual int getTileBelDimZ(int x, int y) const = 0;
    virtual int getTilePipDimZ(int x, int y) const { return 1; }
    virtual char getNameDelimiter() const { return ' '; }

    // Bel methods
    virtual typename R::AllBelsRange getBels() const = 0;
    virtual BelId getBelByName(IdStringList name) const = 0;
    virtual IdStringList getBelName(BelId bel) const = 0;
    virtual uint32_t getBelChecksum(BelId bel) const { return uint32_t(std::hash<BelId>()(bel)); }
    virtual void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
    {
        NPNR_ASSERT(bel != BelId());
        auto &entry = base_bel2cell[bel];
        NPNR_ASSERT(entry == nullptr);
        cell->bel = bel;
        cell->belStrength = strength;
        entry = cell;
        refreshUiBel(bel);
    }
    virtual void unbindBel(BelId bel)
    {
        NPNR_ASSERT(bel != BelId());
        auto &entry = base_bel2cell[bel];
        NPNR_ASSERT(entry != nullptr);
        entry->bel = BelId();
        entry->belStrength = STRENGTH_NONE;
        entry = nullptr;
        refreshUiBel(bel);
    }
    virtual Loc getBelLocation(BelId bel) const = 0;
    virtual BelId getBelByLocation(Loc loc) const = 0;
    virtual typename R::TileBelsRange getBelsByTile(int x, int y) const = 0;
    virtual bool getBelGlobalBuf(BelId bel) const { return false; }
    virtual bool checkBelAvail(BelId bel) const { return getBoundBelCell(bel) == nullptr; };
    virtual CellInfo *getBoundBelCell(BelId bel) const
    {
        auto fnd = base_bel2cell.find(bel);
        return fnd == base_bel2cell.end() ? nullptr : fnd->second;
    }
    virtual CellInfo *getConflictingBelCell(BelId bel) const { return getBoundBelCell(bel); }
    virtual IdString getBelType(BelId bel) const = 0;
    virtual typename R::BelAttrsRange getBelAttrs(BelId bel) const
    {
        return empty_if_possible<typename R::BelAttrsRange>();
    }
    virtual WireId getBelPinWire(BelId bel, IdString pin) const = 0;
    virtual PortType getBelPinType(BelId bel, IdString pin) const = 0;

    // Wire methods
    virtual typename R::AllWiresRange getWires() const = 0;
    virtual WireId getWireByName(IdStringList name) const = 0;
    virtual IdStringList getWireName(WireId wire) const = 0;
    virtual IdString getWireType(WireId wire) const { return IdString(); }
    virtual typename R::WireAttrsRange getWireAttrs(WireId) const
    {
        return empty_if_possible<typename R::WireAttrsRange>();
    }
    virtual uint32_t getWireChecksum(WireId wire) const { return uint32_t(std::hash<WireId>()(wire)); }
    virtual typename R::DownhillPipRange getPipsDownhill(WireId wire) const = 0;
    virtual typename R::UphillPipRange getPipsUphill(WireId wire) const = 0;
    virtual typename R::WireBelPinRange getWireBelPins(WireId wire) const = 0;
    virtual void bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(wire != WireId());
        auto &w2n_entry = base_wire2net[wire];
        NPNR_ASSERT(w2n_entry == nullptr);
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
        w2n_entry = net;
        refreshUiWire(wire);
    }
    virtual void unbindWire(WireId wire)
    {
        NPNR_ASSERT(wire != WireId());
        auto &w2n_entry = base_wire2net[wire];
        NPNR_ASSERT(w2n_entry != nullptr);

        auto &net_wires = w2n_entry->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        auto pip = it->second.pip;
        if (pip != PipId()) {
            base_pip2net[pip] = nullptr;
        }

        net_wires.erase(it);
        base_wire2net[wire] = nullptr;

        w2n_entry = nullptr;
        refreshUiWire(wire);
    }
    virtual bool checkWireAvail(WireId wire) const { return getBoundWireNet(wire) == nullptr; }
    virtual NetInfo *getBoundWireNet(WireId wire) const
    {
        auto fnd = base_wire2net.find(wire);
        return fnd == base_wire2net.end() ? nullptr : fnd->second;
    }
    virtual WireId getConflictingWireWire(WireId wire) const { return wire; };
    virtual NetInfo *getConflictingWireNet(WireId wire) const { return getBoundWireNet(wire); }
    virtual DelayInfo getWireDelay(WireId wire) const = 0;

    // Pip methods
    virtual typename R::AllPipsRange getPips() const = 0;
    virtual PipId getPipByName(IdStringList name) const = 0;
    virtual IdStringList getPipName(PipId pip) const = 0;
    virtual IdString getPipType(PipId pip) const { return IdString(); }
    virtual typename R::PipAttrsRange getPipAttrs(PipId) const
    {
        return empty_if_possible<typename R::PipAttrsRange>();
    }
    virtual uint32_t getPipChecksum(PipId pip) const { return uint32_t(std::hash<PipId>()(pip)); }
    virtual void bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(pip != PipId());
        auto &p2n_entry = base_pip2net[pip];
        NPNR_ASSERT(p2n_entry == nullptr);
        p2n_entry = net;

        WireId dst = getPipDstWire(pip);
        auto &w2n_entry = base_wire2net[dst];
        NPNR_ASSERT(w2n_entry == nullptr);
        w2n_entry = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
    }
    virtual void unbindPip(PipId pip)
    {
        NPNR_ASSERT(pip != PipId());
        auto &p2n_entry = base_pip2net[pip];
        NPNR_ASSERT(p2n_entry != nullptr);
        WireId dst = getPipDstWire(pip);

        auto &w2n_entry = base_wire2net[dst];
        NPNR_ASSERT(w2n_entry != nullptr);
        w2n_entry = nullptr;

        p2n_entry->wires.erase(dst);
        p2n_entry = nullptr;
    }
    virtual bool checkPipAvail(PipId pip) const { return getBoundPipNet(pip) == nullptr; }
    virtual NetInfo *getBoundPipNet(PipId pip) const
    {
        auto fnd = base_pip2net.find(pip);
        return fnd == base_pip2net.end() ? nullptr : fnd->second;
    }
    virtual WireId getConflictingPipWire(PipId pip) const { return WireId(); }
    virtual NetInfo *getConflictingPipNet(PipId pip) const { return getBoundPipNet(pip); }
    virtual WireId getPipSrcWire(PipId pip) const = 0;
    virtual WireId getPipDstWire(PipId pip) const = 0;
    virtual DelayInfo getPipDelay(PipId pip) const = 0;
    virtual Loc getPipLocation(PipId pip) const = 0;

    // Group methods
    virtual GroupId getGroupByName(IdStringList name) const { return GroupId(); };
    virtual IdStringList getGroupName(GroupId group) const { return IdStringList(); };
    virtual delay_t estimateDelay(WireId src, WireId dst) const = 0;
    virtual ArcBounds getRouteBoundingBox(WireId src, WireId dst) const = 0;
    virtual typename R::AllGroupsRange getGroups() const { return empty_if_possible<typename R::AllGroupsRange>(); }
    // Default implementation of these assumes no groups so never called
    virtual typename R::GroupBelsRange getGroupBels(GroupId group) const { NPNR_ASSERT_FALSE("unreachable"); };
    virtual typename R::GroupWiresRange getGroupWires(GroupId group) const { NPNR_ASSERT_FALSE("unreachable"); };
    virtual typename R::GroupPipsRange getGroupPips(GroupId group) const { NPNR_ASSERT_FALSE("unreachable"); };
    virtual typename R::GroupGroupsRange getGroupGroups(GroupId group) const { NPNR_ASSERT_FALSE("unreachable"); };

    // Delay methods
    virtual delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const = 0;
    virtual delay_t getDelayEpsilon() const = 0;
    virtual delay_t getRipupDelayPenalty() const = 0;
    virtual float getDelayNS(delay_t v) const = 0;
    virtual DelayInfo getDelayFromNS(float ns) const = 0;
    virtual uint32_t getDelayChecksum(delay_t v) const = 0;
    virtual bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const
    {
        return false;
    }

    // Decal methods
    virtual typename R::DecalGfxRange getDecalGraphics(DecalId decal) const { NPNR_ASSERT_FALSE("unreachable"); };
    virtual DecalXY getBelDecal(BelId bel) const { return DecalXY(); }
    virtual DecalXY getWireDecal(WireId wire) const { return DecalXY(); }
    virtual DecalXY getPipDecal(PipId pip) const { return DecalXY(); }
    virtual DecalXY getGroupDecal(GroupId group) const { return DecalXY(); }

    // Cell timing methods
    virtual bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
    {
        return false;
    }
    virtual TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
    {
        return TMG_IGNORE;
    }
    virtual TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
    {
        NPNR_ASSERT_FALSE("unreachable");
    }

    // Placement validity checks
    virtual bool isValidBelForCellType(IdString cell_type, BelId bel) const { return cell_type == getBelType(bel); }
    virtual IdString getBelBucketName(BelBucketId bucket) const = 0;
    virtual BelBucketId getBelBucketByName(IdString name) const = 0;
    virtual BelBucketId getBelBucketForBel(BelId bel) const { return getBelBucketForCellType(getBelType(bel)); };
    virtual BelBucketId getBelBucketForCellType(IdString cell_type) const { return getBelBucketByName(cell_type); };
    virtual bool isValidBelForCell(CellInfo *cell, BelId bel) const { return true; }
    virtual bool isBelLocationValid(BelId bel) const { return true; }
    virtual typename R::CellTypeRange getCellTypes() const = 0;
    virtual typename R::BelBucketRange getBelBuckets() const = 0;
    virtual typename R::BucketBelRange getBelsInBucket(BelBucketId bucket) const = 0;

    // Flow methods
    virtual bool pack() = 0;
    virtual bool place() = 0;
    virtual bool route() = 0;
    virtual void assignArchInfo(){};

    // --------------------------------------------------------------
    // These structures are used to provide default implementations of bel/wire/pip binding. Arches might want to
    // replace them with their own, for example to use faster access structures than unordered_map. Arches might also
    // want to add extra checks around these functions
    std::unordered_map<BelId, CellInfo *> base_bel2cell;
    std::unordered_map<WireId, NetInfo *> base_wire2net;
    std::unordered_map<PipId, NetInfo *> base_pip2net;
};

NEXTPNR_NAMESPACE_END

#include "arch.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context : Arch, DeterministicRNG
{
    bool verbose = false;
    bool debug = false;
    bool force = false;

    // Should we disable printing of the location of nets in the critical path?
    bool disable_critical_path_source_print = false;

    Context(ArchArgs args) : Arch(args) { BaseCtx::as_ctx = this; }

    // --------------------------------------------------------------

    WireId getNetinfoSourceWire(const NetInfo *net_info) const;
    WireId getNetinfoSinkWire(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getNetinfoRouteDelay(const NetInfo *net_info, const PortRef &sink) const;

    // provided by router1.cc
    bool checkRoutedDesign() const;
    bool getActualRouteDelay(WireId src_wire, WireId dst_wire, delay_t *delay = nullptr,
                             std::unordered_map<WireId, PipId> *route = nullptr, bool useEstimate = true);

    // --------------------------------------------------------------
    // call after changing hierpath or adding/removing nets and cells
    void fixupHierarchy();

    // --------------------------------------------------------------

    // provided by sdf.cc
    void writeSDF(std::ostream &out, bool cvc_mode = false) const;

    // --------------------------------------------------------------

    // provided by svg.cc
    void writeSVG(const std::string &filename, const std::string &flags = "") const;

    // --------------------------------------------------------------

    uint32_t checksum() const;

    void check() const;
    void archcheck() const;

    template <typename T> T setting(const char *name, T defaultValue)
    {
        IdString new_id = id(name);
        auto found = settings.find(new_id);
        if (found != settings.end())
            return boost::lexical_cast<T>(found->second.is_string ? found->second.as_string()
                                                                  : std::to_string(found->second.as_int64()));
        else
            settings[id(name)] = std::to_string(defaultValue);

        return defaultValue;
    }

    template <typename T> T setting(const char *name) const
    {
        IdString new_id = id(name);
        auto found = settings.find(new_id);
        if (found != settings.end())
            return boost::lexical_cast<T>(found->second.is_string ? found->second.as_string()
                                                                  : std::to_string(found->second.as_int64()));
        else
            throw std::runtime_error("settings does not exists");
    }
};

NEXTPNR_NAMESPACE_END

#endif
