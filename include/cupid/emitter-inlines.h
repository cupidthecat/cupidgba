/**
 * @file      emitter-inlines.h
 * @author    cupidthecat
 * @brief     Repetition macros for table generation.
 * @date      2026-11-16
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#ifndef CUPID_EMITTER_INLINES_H
#define CUPID_EMITTER_INLINES_H

#define DO_4(DIRECTIVE) DIRECTIVE, DIRECTIVE, DIRECTIVE, DIRECTIVE

#define DO_8(DIRECTIVE)                                                                            \
    DIRECTIVE, DIRECTIVE, DIRECTIVE, DIRECTIVE, DIRECTIVE, DIRECTIVE, DIRECTIVE, DIRECTIVE

#define DO_256(DIRECTIVE) DO_4(DO_8(DO_8(DIRECTIVE)))

#define DO_INTERLACE(LEFT, RIGHT) LEFT, RIGHT

#endif /* CUPID_EMITTER_INLINES_H */
