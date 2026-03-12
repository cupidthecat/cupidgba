/**
 * @file emitter-inlines.h
 * @brief Repetition macros for table generation.
 */
#ifndef CUPID_EMITTER_INLINES_H
#define CUPID_EMITTER_INLINES_H

#define DO_4(DIRECTIVE) \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE

#define DO_8(DIRECTIVE) \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE, \
	DIRECTIVE

#define DO_256(DIRECTIVE) \
	DO_4(DO_8(DO_8(DIRECTIVE)))

#define DO_INTERLACE(LEFT, RIGHT) \
	LEFT, \
	RIGHT

#endif /* CUPID_EMITTER_INLINES_H */
