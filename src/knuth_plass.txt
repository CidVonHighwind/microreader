#include <stdlib.h>
#include <math.h>
#include <ctype.h>

#include <cmath>
#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstddef>
#include <cstring>

const unsigned int INF = 10000; // The value representing infinity

#ifdef _WIN32
    #define NEWLINE "\r\n"
#elif defined macintosh // OS 9
    #define NEWLINE "\r"
#else
    #define NEWLINE "\n" // Mac OS X uses \n
#endif

// The different specification types there are
enum SpecType {
    BOX,
    GLUE,
    PENALTY
};

enum Justify {
    LEFT,
    RIGHT,
    CENTER,
    FULL
};

typedef long double WidthType;
typedef unsigned long long int SuperULong;

class Break {
public:
    SuperULong position; // The position (index) in the KnuthPlassParagraph where this break occurs
    SuperULong line;     // The line number that this break is on
    float fitness_class; // The fitness class of this break
    double ratio;        // The ratio to multiply the widths of the lines by in order to fit the entire line in the desired line width
    double demerits;     // The demerit count of break here
    std::shared_ptr<Break> previous;     // Pointer to previous break in the KnuthPlassParagraph

    Break(SuperULong position, SuperULong line, float fitness_class, double demerits, double ratio, std::shared_ptr<Break> previous) {
        this->position = position;
        this->line = line;
        this->fitness_class = fitness_class;
        this->demerits = demerits;
        this->previous = previous;
        this->ratio = ratio;
    }
};


template <typename ValueType>
class KnuthPlassParagraph {
public:
    // -- Specification vectors (used to store specifications i.e. how the paragraph looks)
    std::vector<SpecType>  type;    // t in the paper; the type of the Spec
    std::vector<float>     width;   // w in the paper; the ideal width of the glue, the width of added typeset material for the penalty, or the static width of the box
    std::vector<float>     stretch; // y in the paper; the amount this glue can stretch/enlarge its width by
    std::vector<float>     shrink;  // z in the paper; the amount this glue can shrink its width by
    std::vector<float>     penalty; // p in the paper; the amount to be penalized if use this penalty
    std::vector<bool>      flagged; // f in the paper; used to say whether a hyphen will need to be put here

    std::vector<ValueType> value;   // Whatever value you want to be storing at this point in the paragraph; not used by the algorithm so can be whatever you want

    ~KnuthPlassParagraph() {
        if (sum_shrink != nullptr) {
            free(sum_shrink);
            sum_shrink = nullptr;
        }

        if (sum_stretch != nullptr) {
            free(sum_stretch);
            sum_stretch = nullptr;
        }

        if (sum_width != nullptr) {
            free(sum_width);
            sum_width = nullptr;
        }
    }

    // ------------------------------------------------------------------------
    // adding specifications to the paragraph

    void add_glue(float shrink, float width, float stretch, ValueType value) {
        this->type.push_back(GLUE);

        this->shrink.push_back(shrink);    // width - shrink = minimum width of glue
        this->width.push_back(width);      // ideal width
        this->stretch.push_back(stretch);  // width + stretch = max width of glue

        this->value.push_back(value);

        // defaults/not used by this type
        this->penalty.push_back(0.0);
        this->flagged.push_back(false);
    }

    void add_penalty(float width, float penalty, bool flagged, ValueType value) {
        this->type.push_back(PENALTY);

        this->width.push_back(width);     // width of typeset material (hyphen) if use this penalty
        this->penalty.push_back(penalty); // arbitrary amount of penalty for this item
        this->flagged.push_back(flagged); // if true, then need hyphen if break at this penalty

        this->value.push_back(value);

        // defaults/not used by this type
        this->shrink.push_back(0.0);
        this->stretch.push_back(0.0);
    }

    void add_box(float width, ValueType value) {
        this->type.push_back(BOX);

        this->width.push_back(width); // the width of what this box represents

        this->value.push_back(value);

        // defaults/not used by this type
        this->shrink.push_back(0.0);
        this->stretch.push_back(0.0);
        this->penalty.push_back(0.0);
        this->flagged.push_back(false);
    }

    // ------------------------------------------------------------------------
    // code for user after calculations

    // The returned breaks from calc_knuth_plass_breaks is stored here
    std::vector<std::shared_ptr<Break>> breaks;

    /**
     * Calculates and returns the actual width that the current item should
     * take on when trying to fill the full width of the current line
     */
    double r_width(SuperULong i, double ratio) {
        if (ratio < 0) { return width.at(i) - (ratio * shrink.at(i));  }
        else           { return width.at(i) + (ratio * stretch.at(i)); }
    }

    // ------------------------------------------------------------------------
    // code for actually doing the knuth-plass calculations

    WidthType *sum_width   = nullptr;
    WidthType *sum_stretch = nullptr;
    WidthType *sum_shrink  = nullptr;

    double compute_adjustment_ratio(SuperULong pos1, SuperULong pos2, SuperULong line, std::vector<double> *line_lengths) {
        WidthType ideal_width = sum_width[pos2] - sum_width[pos1];

        if (type[pos2] == PENALTY) {
            ideal_width += width.at(pos2);
        }

        WidthType available_width;

        // Get the length of the current line; if the line_lengths list
        // is too short, the last value is always used for subsequent
        // lines.
        if (line < line_lengths->size()) {
            available_width = line_lengths->at(line);
        } else {
            available_width = line_lengths->back();
        }

        double r;

        // Compute how much the contents of the line would have to be
        // stretched or shrunk to fit into the available space.
        if (ideal_width < available_width) {
            // You would have to stretch this line if you want it to fit on the
            //   desired line
            double y = sum_stretch[pos2] - sum_stretch[pos1]; // get the max stretched length for this line we are considering

            if (y > 0) {
                // Since it is possible to stretch the line, found out how much
                //   you should stretch it by to take up the full width of the line
                r = (available_width - ideal_width) / y;
            } else {
                r = INF;
            }

        } else if (ideal_width > available_width) {
            // Must shrink the line by removing space from glue if you want it
            //   to fit on the line
            double z = sum_shrink[pos2] - sum_shrink[pos1];

            if (z > 0.0) {
                // Since it is possible to shrink the line, find how much you
                //   should shrink it to fit it perfectly (width matches
                //   desired width) on the line
                r = (available_width - ideal_width) / z;
            } else {
                r = INF;
            }
        } else {
            // Exactly the right length!
            r = 0.0;
        }

        return r;
    }

    /**
     * Adds an active node to the given active nodes list.
     */
    void add_active_node(std::vector<std::shared_ptr<Break> > *active_nodes, std::shared_ptr<Break> node) {

        SuperULong length = active_nodes->size();
        SuperULong index = 0;

        // Find the first index at which the active node's line number
        // is equal to or greater than the line for 'node'.  This gives
        // us the insertion point.
        while (index < length && active_nodes->at(index)->line < node->line) {
            index += 1;
        }

        SuperULong insert_index = index;

        // Check if there's a node with the same line number and
        // position and fitness.  This lets us ensure that the list of
        // active nodes always has unique (line, position, fitness)
        // values.
        while (index < length && active_nodes->at(index)->line == node->line) {
            if (active_nodes->at(index)->fitness_class == node->fitness_class
                      && active_nodes->at(index)->line == node->line) {
                // A match, so just return without adding the node, and free
                // the node because we are not going to use it past this point
                // as we are already using a node that is equivalent to it
                return;
            }

            index += 1;
        }

        active_nodes->insert(active_nodes->begin() + insert_index, node);
    }

    /**
     * Returns true if specification at i is a
     *   feasible_breakpoint, false otherwise.
     */
    bool is_feasible_breakpoint(SuperULong i) {
        if (type[i] == PENALTY && penalty[i] < INF) {
            // Forced Breakpoint
            return true;
        } else if (i > 0 && type[i-1] == BOX && type[i] == GLUE) {
            // Breakpoint when glue directly follows a box
            return true;
        } else {
            return false;
        }
    }

    /**
     * Calculates all Knuth-Plass breaks for this KnuthPlass paragraph as it
     *    currently is.
     */
    void calc_knuth_plass_breaks(
            std::vector<double> *line_lengths,
            double looseness      =0.0,
            double tolerance      =1.0,
            double fitness_demerit=100.0,
            double flagged_demerit=100.0
        ) {

        // Free up things from last run (if ran before)
        if (sum_shrink != nullptr) {
            free(sum_shrink);
            sum_shrink = nullptr;
        }

        if (sum_stretch != nullptr) {
            free(sum_stretch);
            sum_stretch = nullptr;
        }

        if (sum_width != nullptr) {
            free(sum_width);
            sum_width = nullptr;
        }

        SuperULong m = type.size(); // number of specifications

        // -- Allocation
        sum_width   = (WidthType *)malloc(m * sizeof(WidthType));
        sum_stretch = (WidthType *)malloc(m * sizeof(WidthType));
        sum_shrink  = (WidthType *)malloc(m * sizeof(WidthType));

        WidthType width_sum, stretch_sum, shrink_sum;
        width_sum = stretch_sum = shrink_sum = 0.0;

        // populate sums
        for (SuperULong i = 0; i < type.size(); i++) {
            sum_width[i]   = width_sum;
            sum_stretch[i] = stretch_sum;
            sum_shrink[i]  = shrink_sum;

            width_sum   += width[i];
            stretch_sum += stretch[i];
            shrink_sum  += shrink[i];
        }

        // vector of active nodes
        std::vector<std::shared_ptr<Break>> active_nodes;

        // Create the first breakpoint at the start of the paragraph
        std::shared_ptr<Break> A = std::make_shared<Break>(0, 0, 1, 0, 0, nullptr);
        active_nodes.push_back(A);

        // -- Begin main portion of the algorithm
        std::vector<std::shared_ptr<Break>> breaks_to_activate; // List of newly-found feasible breaks
        std::vector<std::shared_ptr<Break>> breaks_to_deactivate; // breaks to remove from the list of active breakpoints
        for (SuperULong B = 0; B < m; B++) {
            // Determine if this Specification is a feasible breakpoint and
            // perform the main loop if it is.
            if (is_feasible_breakpoint(B)) {
                // Loop over the list of active nodes, and compute the fitness
                // of the line formed by breaking at A and B.  The resulting
                for (SuperULong j = 0; j < active_nodes.size(); j++) {
                    A = active_nodes.at(j);

                    double r = compute_adjustment_ratio(A->position, B, A->line, line_lengths);

                    // If r makes the line smaller than 1 times the shrinkage
                    //   or B (represented by i) is a forced break, we need to
                    //   deactivate A
                    if (r < -1 || penalty[B] > INF) {
                        breaks_to_deactivate.push_back(A);
                    }

                    if (-1 <= r && r <= tolerance) {
                        // Compute demerits and fitness class
                        float p = penalty[B];
                        double demerits;

                        if (p >= 0) {
                            demerits = (1.0 + 100.0 * std::pow(std::pow(std::abs(r), 3.0) + p, 3.0));
                        } else if (penalty[B] > INF) {
                            demerits = (1.0 + 100.0 * std::pow(std::pow(std::abs(r), 3.0), 2.0) - std::pow(p, 2.0));
                        } else {
                            demerits = (1.0 + 100.0 * std::pow(std::pow(std::abs(r), 3.0), 2.0));
                        }

                        if (flagged[A->position] && flagged[B]) {
                            demerits += flagged_demerit;
                        }

                        // Figure out the fitness class of this line (tight, loose,
                        // very tight, or very loose).
                        float fitness_class;
                        if      (r < -0.5) { fitness_class = 0.0; }
                        else if (r <= 0.5) { fitness_class = 1.0; }
                        else if (r <= 1)   { fitness_class = 2.0; }
                        else               { fitness_class = 3.0; }

                        // If two consecutive lines are in very different fitness
                        // classes, add to the demerit score for this break.
                        if (std::abs(fitness_class - A->fitness_class) > 1) {
                            demerits += fitness_demerit;
                        }

                        breaks_to_activate.push_back(std::make_shared<Break>(
                                    B,
                                    A->line + 1,
                                    fitness_class,
                                    demerits,
                                    r,
                                    A
                                )
                            );
                    }
                }

                // Remove all breaks that need to be removed
                size_t len = breaks_to_deactivate.size();
                for (size_t i = 0; i < len; i++) {
                    if (active_nodes.size() == 1) {
                        // would remove the first active breakpoint so don't do
                        // that
                        break;
                    } else {
                        // Find break_node in active nodes
                        std::shared_ptr<Break> break_node = breaks_to_deactivate.at(i);

                        // Remove pointer to breaknode from the active nodes
                        SuperULong size = active_nodes.size();
                        for (SuperULong k = 0; k < size; k++) {
                            // See if the two pointers are pointing to the
                            // same object. If are, then this is the object
                            // that we want to remove from the active_nodes
                            if (active_nodes.at(k) == break_node) {
                                // found node so remove it from active_nodes
                                active_nodes.erase(active_nodes.begin() + k);
                                break;
                            }
                        }
                    }
                }
                breaks_to_deactivate.clear();

                // add all new breaks that need to be added
                len = breaks_to_activate.size();
                for (size_t i = 0; i < len; i++) {
                    add_active_node(&active_nodes, breaks_to_activate.at(i));
                }
                breaks_to_activate.clear();
            }
        }

        // Find the active node with the lowest number of demerits.
        size_t len = active_nodes.size();
        A = active_nodes.at(0);
        for (size_t i = 0; i < len; i++) {
            if (active_nodes.at(i)->demerits < A->demerits) {
                A = active_nodes.at(i);
            }
        }

        // Handle loosness
        if (looseness != 0) {
            // The search for the appropriate active node is a bit more
            // complicated we look for a node with a paragraph length that's as
            // close as possible to (A.line + looseness) with the minimum
            // number of demerits.

            unsigned int best = 0;
            unsigned int d = INF;
            std::shared_ptr<Break> br;
            std::shared_ptr<Break> b;

            len = active_nodes.size();
            for (size_t i = 0; i < len; i++) {
                br = active_nodes.at(i);

                SuperULong delta = br->line - A->line;

                // The two branches of this 'if' statement are for handling
                // values of looseness that are either positive or negative.
                if ((looseness <= delta && delta < best)
                        || (best < delta || delta < looseness)) {
                    d = br->demerits;
                    b = br;
                } else if (delta == best && br->demerits < d) {
                    // This break is of the same length, but has fewer demerits
                    // and hence is the one we should use.
                    d = br->demerits;
                    b = br;
                }
            }

            A = b;
        }

        // Generate the list of chosen break points
        std::vector<std::shared_ptr<Break>> breaks;
        while (A->previous != nullptr) {
            breaks.push_back(A);
            A = A->previous;
        }
        std::reverse(breaks.begin(), breaks.end()); // reverse it so that index 0 holds break for line 1

        this->breaks = breaks; // Returned values
    }
};

/**
 * Some simple sample code that shows the simplest way to take text and parse
 * it into a KnuthPlassParagraph.
 *
 * The paragraph is passed to you to dispose of.
 */
KnuthPlassParagraph<char> *make_simple_paragraph(const char *text) {
    KnuthPlassParagraph<char> *par = new KnuthPlassParagraph<char>();

    size_t size = strlen(text);

    for (size_t i = 0; i < size; i++) {
        char c = *(text + i);

        switch (c) {
            case '\n':
            case '\t':
            case '\r':
            case '\v':
            case '\f':
            case  ' ':
                // Add space worth of width
                par->add_glue(1, 2, 1, ' ');
                break;

            case '@':
                // Forced break
                par->add_penalty(0, -INF, false, c);
                break;

            case '~':
                // Forced NOT to break here
                par->add_penalty(0, INF, false, c);
                break;

            default:
                // All characters are 1 unit wide
                par->add_box(1, c);
        }
    }

    // Add standard paragraph end
    par->add_penalty(0,  INF,   0, ' '); // Forced non-break (must not break here, otherwise a Box coming before the Glue after this would allow a break to be here)
    par->add_glue(   0,    0, INF, ' '); // Glue that fills the rest of the last line (even if that fill is 0 width)
    par->add_penalty(0, -INF,   1, ' '); // Forced break (Ends last line)

    return par;
}

/**
 * Inserts more spaces into the whitespaces that already exist in str.
 */
void insert_spaces(std::string *str, size_t num_spaces) {
    while (true) {
        std::string out;
        bool added_space = false;
        bool add_space = false;

        size_t size = str->size();
        for (size_t i = 0; i < size; i++) {
            char ch = str->at(i);

            // Add a space before this whitespace if the current char is a whitespace
            if (num_spaces > 0 && add_space && isspace(ch)) {
                out += ' ';
                num_spaces -= 1;
                added_space = true;
                add_space = false;
            } else {
                added_space = true;
            }

            out += ch;
        }

        if (!added_space) {
            out += ' ';
            num_spaces -= 1;
        }

        if (num_spaces <= 0) {
            break;
        }

        str->clear();
        str->append(out);
    }
}

/*
 * Prints a KnuthPlassParagraph made by the make_simple_paragraph function and
 * assuming that the paragraph has already had its knuth_plass_breaks
 * calculated by the time it is passed in to this function
 */
std::string * str_for_simple_paragraph(KnuthPlassParagraph <char> * par, Justify justified=LEFT, std::string *end_mark=nullptr) {
    std::string *out = new std::string();
    std::string curr_line;
    std::shared_ptr<Break> curr_break;

    size_t len = par->breaks.size();
    size_t j = 0;
    unsigned long spaces_needed;

    for (size_t i = 0; i < len; i++) {
        curr_break = par->breaks.at(i);

        // Build the line
        size_t end_pos = curr_break->position;
        while (j < end_pos) {
            switch (par->type.at(j)) {
                case GLUE:
                    if (justified == FULL) {
                        spaces_needed = par->r_width(i, curr_break->ratio);
                        std::cout << spaces_needed << std::endl;
                    } else {
                        spaces_needed = 1; // not full justified so only need 1 space per glue
                    }

                    for (size_t k = 0; k < spaces_needed; k++) {
                        curr_line += ' ';
                    }
                    break;
                case BOX:
                    // always append the char
                    curr_line += par->value.at(j);
                    break;
                case PENALTY:
                    if (par->flagged.at(j) && j == curr_break->position) {
                        curr_line += '-';
                    }
                    break;
            }

            j++;
        } // End of loop that builds the current line

        if (end_mark != nullptr) {
            curr_line.append(*end_mark);
        }
        curr_line.append(NEWLINE); // End the current line

        out->append(curr_line);
        curr_line.clear();

        // Now j is at the start of the next line

        std::cout << curr_break->position << std::endl;
        std::cout << curr_break->line << std::endl;
        std::cout << curr_break->ratio << std::endl;
    }

    return out;
}

int main() {
    //const char* short_text = "Among other public buildings in a certain town, which for many reasons it will be prudent to refrain from mentioning, and to which I will assign no fictitious name, there is one anciently common to most towns, great or small: to wit, a workhouse; and in this workhouse was born; on a day and date which I need not trouble myself to repeat, inasmuch as it can be of no possible consequence to the reader, in this stage of the business at all events; the item of mortality whose name is prefixed to the head of this chapter.";
    const char* medium_text = "For the next eight or ten months, Oliver was the victim of a systematic course of treachery and deception. He was brought up by hand. The hungry and destitute situation of the infant orphan was duly reported by the workhouse authorities to the parish authorities. The parish authorities inquired with dignity of the workhouse authorities, whether there was no female then domiciled in “the house” who was in a situation to impart to Oliver Twist, the consolation and nourishment of which he stood in need. The workhouse authorities replied with humility, that there was not. Upon this, the parish authorities magnanimously and humanely resolved, that Oliver should be “farmed,” or, in other words, that he should be dispatched to a branch-workhouse some three miles off, where twenty or thirty other juvenile offenders against the poor-laws, rolled about the floor all day, without the inconvenience of too much food or too much clothing, under the parental superintendence of an elderly female, who received the culprits at and for the consideration of sevenpence-halfpenny per small head per week. Sevenpence-halfpenny’s worth per week is a good round diet for a child; a great deal may be got for sevenpence-halfpenny, quite enough to overload its stomach, and make it uncomfortable. The elderly female was a woman of wisdom and experience; she knew what was good for children; and she had a very accurate perception of what was good for herself. So, she appropriated the greater part of the weekly stipend to her own use, and consigned the rising parochial generation to even a shorter allowance than was originally provided for them. Thereby finding in the lowest depth a deeper still; and proving herself a very great experimental philosopher.";

    KnuthPlassParagraph <char> *paragraph = make_simple_paragraph(medium_text);

    std::cout << "Finished Parsing short_text" << std::endl;

    // Figure out line lengths. Here, I am asking for every line to be about
    // 100 units long
    std::vector<double> line_lengths;
    line_lengths.push_back(100.0);

    std::string end_mark("|");

    std::cout << "Calculating KnuthPlassParagraph breaks..." << std::endl;

    paragraph->calc_knuth_plass_breaks(&line_lengths);

    std::cout << "Printing out simple paragraph..." << std::endl;
    std::cout << *str_for_simple_paragraph(paragraph, LEFT, &end_mark) << std::endl;
    delete paragraph;
    std::cout << "...DONE" << std::endl;
    return 0;
}








