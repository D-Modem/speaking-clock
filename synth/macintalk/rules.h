/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * English letter-to-sound rules.  These are the rules of H. S. Elovitz,
 * R. Johnson, A. McHugh and J. E. Shore, "Letter-to-Sound Rules for
 * Automatic Translation of English Text to Phonetics", NRL Report 7948
 * (US Naval Research Laboratory, 1976), a US government work in the public
 * domain, written out again here.  Nothing in this file comes from Apple.
 *
 * Each rule is { left context, letters, right context, phonemes }.  Context
 * symbols:  ' ' a word boundary, '#' one or more vowels, ':' zero or more
 * consonants, '^' one consonant, '.' a voiced consonant (B D G J L M N R V
 * W Z), '+' a front vowel (E I Y), '%' a suffix (ER E ES ED ING ELY), '&' a
 * sibilant (S C G Z X J CH SH), '@' T S R D L Z N J TH CH SH.  The rules
 * for a letter are tried in order and the first that fits is used.
 */

struct rule { const char *left, *match, *right, *out; };

static const struct rule rules_punct[] = {
	{ "", "'", "", "" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_a[] = {
	{ "", "A", " ", "AX" },
	{ " ", "ARE", " ", "AA r" },
	{ " ", "AR", "O", "AX r" },
	{ "", "AR", "#", "EH r" },
	{ " ^", "AS", "#", "EY s" },
	{ "", "A", "WA", "AX" },
	{ "", "AW", "", "AO" },
	{ " :", "ANY", "", "EH n IY" },
	{ "", "A", "^+#", "EY" },
	{ "#:", "ALLY", "", "AX l IY" },
	{ " ", "AL", "#", "AX l" },
	{ "", "AGAIN", "", "AX g EH n" },
	{ "#:", "AG", "E", "IH J" },
	{ "", "A", "^+:#", "AE" },
	{ " :", "A", "^+ ", "EY" },
	{ "", "A", "^%", "EY" },
	{ " ", "ARR", "", "AX r" },
	{ "", "ARR", "", "AE r" },
	{ " :", "AR", " ", "AA r" },
	{ "", "AR", " ", "ER" },
	{ "", "AR", "", "AA r" },
	{ "", "AIR", "", "EH r" },
	{ "", "AI", "", "EY" },
	{ "", "AY", "", "EY" },
	{ "", "AU", "", "AO" },
	{ "#:", "AL", " ", "AX l" },
	{ "#:", "ALS", " ", "AX l z" },
	{ "", "ALK", "", "AO k" },
	{ "", "AL", "^", "AO l" },
	{ " :", "ABLE", "", "EY b AX l" },
	{ "", "ABLE", "", "AX b AX l" },
	{ "", "ANG", "+", "EY n J" },
	{ "", "A", "", "AE" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_b[] = {
	{ " ", "BE", "^#", "b IH" },
	{ "", "BEING", "", "b IY IH N" },
	{ " ", "BOTH", " ", "b OW T" },
	{ " ", "BUS", "#", "b IH z" },
	{ "", "BUIL", "", "b IH l" },
	{ "", "B", "", "b" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_c[] = {
	{ " ", "CH", "^", "k" },
	{ "^E", "CH", "", "k" },
	{ "", "CH", "", "C" },
	{ " S", "CI", "#", "s AY" },
	{ "", "CI", "A", "S" },
	{ "", "CI", "O", "S" },
	{ "", "CI", "EN", "S" },
	{ "", "C", "+", "s" },
	{ "", "CK", "", "k" },
	{ "", "COM", "%", "k AH m" },
	{ "", "C", "", "k" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_d[] = {
	{ "#:", "DED", " ", "d IH d" },
	{ ".E", "D", " ", "d" },
	{ "#^:E", "D", " ", "t" },
	{ " ", "DE", "^#", "d IH" },
	{ " ", "DO", " ", "d UW" },
	{ " ", "DOES", "", "d AH z" },
	{ " ", "DOING", "", "d UW IH N" },
	{ " ", "DOW", "", "d AW" },
	{ "", "DU", "A", "J UW" },
	{ "", "D", "", "d" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_e[] = {
	{ "#:", "E", " ", "" },
	{ "'^:", "E", " ", "" },
	{ " :", "E", " ", "IY" },
	{ "#", "ED", " ", "d" },
	{ "#:", "E", "D ", "" },
	{ "", "EV", "ER", "EH v" },
	{ "", "E", "^%", "IY" },
	{ "", "ERI", "#", "IY r IY" },
	{ "", "ERI", "", "EH r IH" },
	{ "#:", "ER", "#", "ER" },
	{ "", "ER", "#", "EH r" },
	{ "", "ER", "", "ER" },
	{ " ", "EVEN", "", "IY v EH n" },
	{ "#:", "E", "W", "" },
	{ "@", "EW", "", "UW" },
	{ "", "EW", "", "y UW" },
	{ "", "E", "O", "IY" },
	{ "#:&", "ES", " ", "IH z" },
	{ "#:", "E", "S ", "" },
	{ "#:", "ELY", " ", "l IY" },
	{ "#:", "EMENT", "", "m EH n t" },
	{ "", "EFUL", "", "f UH l" },
	{ "", "EE", "", "IY" },
	{ "", "EARN", "", "ER n" },
	{ " ", "EAR", "^", "ER" },
	{ "", "EAD", "", "EH d" },
	{ "#:", "EA", " ", "IY AX" },
	{ "", "EA", "SU", "EH" },
	{ "", "EA", "", "IY" },
	{ "", "EIGH", "", "EY" },
	{ "", "EI", "", "IY" },
	{ " ", "EYE", "", "AY" },
	{ "", "EY", "", "IY" },
	{ "", "EU", "", "y UW" },
	{ "", "E", "", "EH" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_f[] = {
	{ "", "FUL", "", "f UH l" },
	{ "", "F", "", "f" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_g[] = {
	{ "", "GIV", "", "g IH v" },
	{ " ", "G", "I^", "g" },
	{ "", "GE", "T", "g EH" },
	{ "SU", "GGES", "", "g J EH s" },
	{ "", "GG", "", "g" },
	{ " B#", "G", "", "g" },
	{ "", "G", "+", "J" },
	{ "", "GREAT", "", "g r EY t" },
	{ "#", "GH", "", "" },
	{ "", "G", "", "g" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_h[] = {
	{ " ", "HAV", "", "h AE v" },
	{ " ", "HERE", "", "h IY r" },
	{ " ", "HOUR", "", "AW ER" },
	{ "", "HOW", "", "h AW" },
	{ "", "H", "#", "h" },
	{ "", "H", "", "" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_i[] = {
	{ " ", "IN", "", "IH n" },
	{ " ", "I", " ", "AY" },
	{ "", "IN", "D", "AY n" },
	{ "", "IER", "", "IY ER" },
	{ "#:R", "IED", "", "IY d" },
	{ "", "IED", " ", "AY d" },
	{ "", "IEN", "", "IY EH n" },
	{ "", "IE", "T", "AY EH" },
	{ " :", "I", "%", "AY" },
	{ "", "I", "%", "IY" },
	{ "", "IE", "", "IY" },
	{ "", "I", "^+:#", "IH" },
	{ "", "IR", "#", "AY r" },
	{ "", "IZ", "%", "AY z" },
	{ "", "IS", "%", "AY z" },
	{ "", "I", "D%", "AY" },
	{ "+^", "I", "^+", "IH" },
	{ "", "I", "T%", "AY" },
	{ "#:^", "I", "^+", "IH" },
	{ "", "I", "^+", "AY" },
	{ "", "IR", "", "ER" },
	{ "", "IGH", "", "AY" },
	{ "", "ILD", "", "AY l d" },
	{ "", "IGN", " ", "AY n" },
	{ "", "IGN", "^", "AY n" },
	{ "", "IGN", "%", "AY n" },
	{ "", "IQUE", "", "IY k" },
	{ "", "I", "", "IH" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_j[] = {
	{ "", "J", "", "J" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_k[] = {
	{ " ", "K", "N", "" },
	{ "", "K", "", "k" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_l[] = {
	{ "", "LO", "C#", "l OW" },
	{ "L", "L", "", "" },
	{ "#:^", "L", "%", "AX l" },
	{ "", "LEAD", "", "l IY d" },
	{ "", "L", "", "l" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_m[] = {
	{ "", "MOV", "", "m UW v" },
	{ "", "M", "", "m" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_n[] = {
	{ "E", "NG", "+", "n J" },
	{ "", "NG", "R", "N g" },
	{ "", "NG", "#", "N g" },
	{ "", "NGL", "%", "N g AX l" },
	{ "", "NG", "", "N" },
	{ "", "NK", "", "N k" },
	{ " ", "NOW", " ", "n AW" },
	{ "", "N", "", "n" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_o[] = {
	{ "", "OF", " ", "AX v" },
	{ "", "OROUGH", "", "ER OW" },
	{ "#:", "OR", " ", "ER" },
	{ "#:", "ORS", " ", "ER z" },
	{ "", "OR", "", "AO r" },
	{ " ", "ONE", "", "w AH n" },
	{ "", "OW", "", "OW" },
	{ " ", "OVER", "", "OW v ER" },
	{ "", "OV", "", "AH v" },
	{ "", "O", "^%", "OW" },
	{ "", "O", "^EN", "OW" },
	{ "", "O", "^I#", "OW" },
	{ "", "OL", "D", "OW l" },
	{ "", "OUGHT", "", "AO t" },
	{ "", "OUGH", "", "AH f" },
	{ " ", "OU", "", "AW" },
	{ "H", "OU", "S#", "AW" },
	{ "", "OUS", "", "AX s" },
	{ "", "OUR", "", "AO r" },
	{ "", "OULD", "", "UH d" },
	{ "^", "OU", "^L", "AH" },
	{ "", "OUP", "", "UW p" },
	{ "", "OU", "", "AW" },
	{ "", "OY", "", "OY" },
	{ "", "OING", "", "OW IH N" },
	{ "", "OI", "", "OY" },
	{ "", "OOR", "", "AO r" },
	{ "", "OOK", "", "UH k" },
	{ "", "OOD", "", "UH d" },
	{ "", "OO", "", "UW" },
	{ "", "O", "E", "OW" },
	{ "", "O", " ", "OW" },
	{ "", "OA", "", "OW" },
	{ " ", "ONLY", "", "OW n l IY" },
	{ " ", "ONCE", "", "w AH n s" },
	{ "", "ON'T", "", "OW n t" },
	{ "C", "O", "N", "AA" },
	{ "", "O", "NG", "AO" },
	{ " :^", "O", "N", "AH" },
	{ "I", "ON", "", "AX n" },
	{ "#:", "ON", " ", "AX n" },
	{ "#^", "ON", "", "AX n" },
	{ "", "O", "ST ", "OW" },
	{ "", "OF", "^", "AO f" },
	{ "", "OTHER", "", "AH D ER" },
	{ "", "OSS", " ", "AO s" },
	{ "#:^", "OM", "", "AH m" },
	{ "", "O", "", "AA" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_p[] = {
	{ "", "PH", "", "f" },
	{ "", "PEOP", "", "p IY p" },
	{ "", "POW", "", "p AW" },
	{ "", "PUT", " ", "p UH t" },
	{ "", "P", "", "p" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_q[] = {
	{ "", "QUAR", "", "k w AO r" },
	{ "", "QU", "", "k w" },
	{ "", "Q", "", "k" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_r[] = {
	{ " ", "RE", "^#", "r IY" },
	{ "", "R", "", "r" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_s[] = {
	{ "", "SH", "", "S" },
	{ "#", "SION", "", "Z AX n" },
	{ "", "SOME", "", "s AH m" },
	{ "#", "SUR", "#", "Z ER" },
	{ "", "SUR", "#", "S ER" },
	{ "#", "SU", "#", "Z UW" },
	{ "#", "SSU", "#", "S UW" },
	{ "#", "SED", " ", "z d" },
	{ "#", "S", "#", "z" },
	{ "", "SAID", "", "s EH d" },
	{ "^", "SION", "", "S AX n" },
	{ "", "S", "S", "" },
	{ ".", "S", " ", "z" },
	{ "#:.E", "S", " ", "z" },
	{ "#:^##", "S", " ", "z" },
	{ "#:^#", "S", " ", "s" },
	{ "U", "S", " ", "s" },
	{ " :#", "S", " ", "z" },
	{ " ", "SCH", "", "s k" },
	{ "", "S", "C+", "" },
	{ "#", "SM", "", "z m" },
	{ "#", "SN", "'", "z AX n" },
	{ "", "S", "", "s" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_t[] = {
	{ " ", "THE", " ", "D AX" },
	{ "", "TO", " ", "t UW" },
	{ "", "THAT", " ", "D AE t" },
	{ " ", "THIS", " ", "D IH s" },
	{ " ", "THEY", "", "D EY" },
	{ " ", "THERE", "", "D EH r" },
	{ "", "THER", "", "D ER" },
	{ "", "THEIR", "", "D EH r" },
	{ " ", "THAN", " ", "D AE n" },
	{ " ", "THEM", " ", "D EH m" },
	{ "", "THESE", " ", "D IY z" },
	{ " ", "THEN", "", "D EH n" },
	{ "", "THROUGH", "", "T r UW" },
	{ "", "THOSE", "", "D OW z" },
	{ "", "THOUGH", " ", "D OW" },
	{ " ", "THUS", "", "D AH s" },
	{ "", "TH", "", "T" },
	{ "#:", "TED", " ", "t IH d" },
	{ "S", "TI", "#N", "C" },
	{ "", "TI", "O", "S" },
	{ "", "TI", "A", "S" },
	{ "", "TIEN", "", "S AX n" },
	{ "", "TUR", "#", "C ER" },
	{ "", "TU", "A", "C UW" },
	{ " ", "TWO", "", "t UW" },
	{ "", "T", "", "t" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_u[] = {
	{ " ", "UN", "I", "y UW n" },
	{ " ", "UN", "", "AH n" },
	{ " ", "UPON", "", "AX p AO n" },
	{ "@", "UR", "#", "UH r" },
	{ "", "UR", "#", "y UH r" },
	{ "", "UR", "", "ER" },
	{ "", "U", "^ ", "AH" },
	{ "", "U", "^^", "AH" },
	{ "", "UY", "", "AY" },
	{ " G", "U", "#", "" },
	{ "G", "U", "%", "" },
	{ "G", "U", "#", "w" },
	{ "#N", "U", "", "y UW" },
	{ "@", "U", "", "UW" },
	{ "", "U", "", "y UW" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_v[] = {
	{ "", "VIEW", "", "v y UW" },
	{ "", "V", "", "v" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_w[] = {
	{ " ", "WERE", "", "w ER" },
	{ "", "WA", "S", "w AA" },
	{ "", "WA", "T", "w AA" },
	{ "", "WHERE", "", "w EH r" },
	{ "", "WHAT", "", "w AA t" },
	{ "", "WHOL", "", "h OW l" },
	{ "", "WHO", "", "h UW" },
	{ "", "WH", "", "w" },
	{ "", "WAR", "", "w AO r" },
	{ "", "WOR", "^", "w ER" },
	{ "", "WR", "", "r" },
	{ "", "W", "", "w" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_x[] = {
	{ "", "X", "", "k s" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_y[] = {
	{ "", "YOUNG", "", "y AH N" },
	{ " ", "YOU", "", "y UW" },
	{ " ", "YES", "", "y EH s" },
	{ " ", "Y", "", "y" },
	{ "#:^", "Y", " ", "IY" },
	{ "#:^", "Y", "I", "IY" },
	{ " :", "Y", " ", "AY" },
	{ " :", "Y", "#", "AY" },
	{ " :", "Y", "^+:#", "IH" },
	{ " :", "Y", "^#", "AY" },
	{ "", "Y", "", "IH" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule rules_z[] = {
	{ "", "Z", "", "z" },
	{ NULL, NULL, NULL, NULL },
};

static const struct rule *const rules[26] = {
	rules_a, rules_b, rules_c, rules_d, rules_e, rules_f, rules_g,
	rules_h, rules_i, rules_j, rules_k, rules_l, rules_m, rules_n,
	rules_o, rules_p, rules_q, rules_r, rules_s, rules_t, rules_u,
	rules_v, rules_w, rules_x, rules_y, rules_z,
};

/* Words the rules get wrong, and the words a clock says, with their
 * stress: a 1 before a vowel is the main stress.  Function words have
 * none. */
static const struct { const char *word, *ph; } lexicon[] = {
	{ "a", "AX" }, { "an", "AX n" }, { "and", "AE n d" }, { "at", "AE t" },
	{ "the", "D AX" }, { "of", "AX v" }, { "to", "t UW" }, { "it", "IH t" },
	{ "is", "IH z" }, { "be", "b IY" }, { "will", "w IH l" }, { "in", "IH n" },
	{ "i", "1AY" }, { "you", "y UW" }, { "are", "AA r" }, { "was", "w AH z" },
	{ "it's", "IH t s" }, { "its", "IH t s" }, { "from", "f r AH m" },
	{ "for", "f AO r" }, { "this", "D IH s" }, { "with", "w IH T" },
	{ "have", "h AE v" }, { "has", "h AE z" }, { "do", "d UW" },
	{ "hello", "h EH l 1OW" }, { "computer", "k AX m p y 1UW t ER" },
	{ "zero", "z 1IY r OW" }, { "oh", "1OW" }, { "one", "w 1AH n" },
	{ "two", "t 1UW" }, { "three", "T r 1IY" }, { "four", "f 1AO r" },
	{ "five", "f 1AY v" }, { "six", "s 1IH k s" }, { "seven", "s 1EH v AX n" },
	{ "eight", "1EY t" }, { "nine", "n 1AY n" }, { "ten", "t 1EH n" },
	{ "eleven", "IH l 1EH v AX n" }, { "twelve", "t w 1EH l v" },
	{ "thirteen", "T ER t 1IY n" }, { "fourteen", "f AO r t 1IY n" },
	{ "fifteen", "f IH f t 1IY n" }, { "sixteen", "s IH k s t 1IY n" },
	{ "seventeen", "s EH v AX n t 1IY n" }, { "eighteen", "EY t 1IY n" },
	{ "nineteen", "n AY n t 1IY n" }, { "twenty", "t w 1EH n t IY" },
	{ "thirty", "T 1ER t IY" }, { "forty", "f 1AO r t IY" },
	{ "fifty", "f 1IH f t IY" }, { "sixty", "s 1IH k s t IY" },
	{ "seventy", "s 1EH v AX n t IY" }, { "eighty", "1EY t IY" },
	{ "ninety", "n 1AY n t IY" }, { "hundred", "h 1AH n d r IX d" },
	{ "thousand", "T 1AW z AX n d" },
	{ "third", "T 1ER d" }, { "stroke", "s t r 1OW k" },
	{ "second", "s 1EH k AX n d" }, { "seconds", "s 1EH k AX n d z" },
	{ "precisely", "p r IY s 1AY s l IY" }, { "exactly", "IH g z 1AE k t l IY" },
	{ "o'clock", "AX k l 1AA k" }, { "oclock", "AX k l 1AA k" },
	{ "noon", "n 1UW n" }, { "midnight", "m 1IH d n AY t" },
	{ "good", "g 1UH d" }, { "morning", "m 1AO r n IH N" },
	{ "afternoon", "AE f t ER n 1UW n" }, { "evening", "1IY v n IH N" },
	{ "time", "t 1AY m" }, { "minute", "m 1IH n IX t" },
	{ "minutes", "m 1IH n IX t s" }, { "hour", "1AW ER" }, { "hours", "1AW ER z" },
	{ "a.m.", "1EY EH m" }, { "p.m.", "p 1IY EH m" }, { "am", "1EY EH m" },
	{ "pm", "p 1IY EH m" }, { "tone", "t 1OW n" }, { "beep", "b 1IY p" },
	{ "clock", "k l 1AA k" }, { "talking", "t 1AO k IH N" },
	{ "speaking", "s p 1IY k IH N" }, { "macintalk", "m 1AE k IH n t AO k" },
	{ "macintosh", "m 1AE k IH n t AA S" }, { "mac", "m 1AE k" },
	{ "apple", "1AE p AX l" }, { "pizza", "p 1IY t s AX" },
	{ "favorite", "f 1EY v r IX t" }, { "food", "f 1UW d" },
	{ "voice", "v 1OY s" }, { "phone", "f 1OW n" }, { "modem", "m 1OW d EH m" },
	{ "said", "s 1EH d" }, { "says", "s 1EH z" }, { "people", "p 1IY p AX l" },
	{ "there", "D EH r" }, { "their", "D EH r" }, { "what", "w 1AH t" },
	{ "where", "w 1EH r" }, { "who", "h 1UW" }, { "why", "w 1AY" },
	{ "they", "D EY" }, { "we", "w IY" }, { "me", "m IY" }, { "my", "m AY" },
	{ "he", "h IY" }, { "she", "S IY" }, { "or", "AO r" }, { "but", "b AH t" },
	{ "not", "n 1AA t" }, { "no", "n 1OW" }, { "yes", "y 1EH s" },
	{ "welcome", "w 1EH l k AX m" }, { "please", "p l 1IY z" },
	{ "thank", "T 1AE N k" }, { "thanks", "T 1AE N k s" },
	{ "sure", "S 1UH r" }, { "like", "l 1AY k" }, { "being", "b 1IY IH N" },
	{ "inside", "IH n s 1AY d" }, { "fancy", "f 1AE n s IY" },
	{ NULL, NULL },
};
