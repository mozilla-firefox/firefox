/*
 * Copyright © 2019-2026  Ebrahim Byagowi
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 */

#ifndef HB_NUMBER_PARSER_HH
#define HB_NUMBER_PARSER_HH

#include "hb.hh"

static const double _small_powers_of_10[] =
{ 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15 };
static const double _large_powers_of_10[] =
{ 1e0, 1e16, 1e32, 1e48, 1e64, 1e80, 1e96, 1e112, 1e128, 1e144,
  1e160, 1e176, 1e192, 1e208, 1e224, 1e240, 1e256, 1e272, 1e288, 1e304 };

/* Grammar:
 *   number  := sign? mantissa exponent?
 *   sign    := '+' | '-'
 *   mantissa:= digits ('.' digits?)?
 *            | '.' digits
 *   exponent:= ('e'|'E') sign? digits
 */
struct hb_number_parser_t
{
  /* Returns true if c was accepted; false ends the parse (c not consumed). */
  bool consume (char c)
  {
    switch (state)
    {
      case Start:
	if (c == '+' || c == '-') { neg = (c == '-'); state = Sign; pos++; return true; }
	if ('0' <= c && c <= '9') { value = value * 10. + (c - '0'); state = IntDigits; pos++; return true; }
	if (c == '.') { state = AfterDotFromStart; pos++; return true; }
	return false;

      case Sign:
	if ('0' <= c && c <= '9') { value = value * 10. + (c - '0'); state = IntDigits; pos++; return true; }
	if (c == '.') { state = AfterDotFromStart; pos++; return true; }
	return false;

      case IntDigits:
	if ('0' <= c && c <= '9') { value = value * 10. + (c - '0'); pos++; return true; }
	if (c == '.') { state = AfterDotFromInt; pos++; return true; }
	if (c == 'e' || c == 'E') { enter_exponent (); return true; }
	return false;

      case AfterDotFromInt:
	if ('0' <= c && c <= '9') { fraction.add (c - '0'); state = FracDigits; pos++; return true; }
	if (c == 'e' || c == 'E') { enter_exponent (); return true; }
	return false;

      case AfterDotFromStart:
	if ('0' <= c && c <= '9') { fraction.add (c - '0'); state = FracDigits; pos++; return true; }
	return false;

      case FracDigits:
	if ('0' <= c && c <= '9') { fraction.add (c - '0'); pos++; return true; }
	if (c == 'e' || c == 'E') { enter_exponent (); return true; }
	return false;

      case ExpStart:
	if (c == '+' || c == '-') { exp_neg = (c == '-'); state = ExpSign; pos++; return true; }
	if ('0' <= c && c <= '9') { exp = c - '0'; state = ExpDigits; pos++; return true; }
	rollback_exponent ();
	return false;

      case ExpSign:
	if ('0' <= c && c <= '9') { exp = c - '0'; state = ExpDigits; pos++; return true; }
	rollback_exponent ();
	return false;

      case ExpDigits:
	if ('0' <= c && c <= '9')
	{
	  constexpr unsigned MAX_EXP = 0x7FFu; /* 2^11-1 */
	  if (likely (exp * 10 + (c - '0') <= MAX_EXP)) exp = exp * 10 + (c - '0');
	  else exp_overflow = true;
	  pos++;
	  return true;
	}
	return false;
    }
    return false;
  }

  /* Signal end of input.  Rolls back a dangling exponent.  Returns the
   * accepted length, or 0 if no valid number was parsed. */
  HB_NODISCARD size_t commit ()
  {
    if (state == ExpStart || state == ExpSign) rollback_exponent ();
    return (state == Sign || state == AfterDotFromStart) ? 0 : pos;
  }

  /* Valid after `commit ()` returned nonzero. */
  double get_value () const
  {
    double v = value;
    if (!fraction.is_empty ()) v += fraction.get_value ();
    if (neg) v *= -1.;

    /* Zero mantissa: any exponent gives zero, and this avoids
     * 0 * HUGE_VAL = nan for "0e441" where _pow10(441) is HUGE_VAL
     * but exp_overflow didn't fire.  Preserves -0.0 for "-0e<...>". */
    if (v == 0) return v;

    if (unlikely (exp_overflow))
    {
      if (exp_neg) return neg ? -.0 : .0;
      else         return neg ? -HUGE_VAL : HUGE_VAL;
    }

    if (exp)
    {
      unsigned e = exp;
      if (exp_neg)
      {
	/* 10^308 is the largest finite power of ten, so two reductions of 308 suffice. */
	if (unlikely (e > 308))
	{
	  v /= 1e308; e -= 308;
	  if (unlikely (e > 308)) { v /= 1e308; e -= 308; }
	}
	v /= _pow10 (e);
      }
      else
	v *= _pow10 (e);
    }
    return v;
  }

private:
  static constexpr uint64_t MAX_FRACT = 10000000000000000000ull; // 10^19
  struct /* fraction */
  {
    void add (int digit)
    {
      if (likely (fraction <= MAX_FRACT / 10))
      {
	fraction = fraction * 10 + digit;
	++count;
      }
      else if (drop < 0) drop = digit;
      else if (digit != 0) sticky = true;
    }

    double get_value () const
    {
      /* (double) fraction rounds to nearest; division by an exact
       * power of ten rounds once more.  Combined error < 1 ULP. */
      double result = (double) fraction;
      if (drop > 5 || (drop == 5 && (sticky || (fraction & 1))))
	result += 1;
      return result / _pow10 (count);
    }

    bool is_empty () const { return !count; }

  private:
    unsigned count = 0;
    uint64_t fraction = 0;
    int drop = -1; /* drop is the first digit we couldn't accumulate */
    bool sticky = false; /* sticky is set when any later digit was nonzero. */
  } fraction;

  enum /* State */
  {
    Start, Sign, IntDigits,
    AfterDotFromInt, AfterDotFromStart, FracDigits,
    ExpStart, ExpSign, ExpDigits,
  } state = Start, pre_exp_state = Start;

  double value = 0;
  unsigned exp = 0;
  bool neg = false, exp_neg = false, exp_overflow = false, exp_speculation_active = false;
  size_t pos = 0, pre_exp_pos = 0;

  void enter_exponent ()
  {
    pre_exp_state = state;
    pre_exp_pos = pos;
    exp_speculation_active = true;
    state = ExpStart;
    pos++;
  }

  void rollback_exponent ()
  {
    if (unlikely (!exp_speculation_active)) return;
    state = pre_exp_state;
    pos = pre_exp_pos;
    exp = 0;
    exp_neg = false;
    exp_overflow = false;
    exp_speculation_active = false;
  }

  /* Handles 0..308; anything larger overflows double and returns HUGE_VAL. */
  static double _pow10 (unsigned n)
  {
    /* DBL_MAX is between 10^308 and 10^309 */
    constexpr unsigned MAX_POW10_EXP = 308;
    constexpr unsigned POW10_SHIFT   = 4;
    constexpr unsigned POW10_SMALL   = 1u << POW10_SHIFT;   /* 16 */
    constexpr unsigned POW10_MASK    = POW10_SMALL - 1;     /* 15 */

    static_assert (ARRAY_LENGTH (_small_powers_of_10) == POW10_SMALL,
		   "_small_powers_of_10: size must be 2^POW10_SHIFT");
    static_assert ((MAX_POW10_EXP >> POW10_SHIFT) < ARRAY_LENGTH (_large_powers_of_10),
		   "_large_powers_of_10: must cover MAX_POW10_EXP >> POW10_SHIFT");

    if (unlikely (n > MAX_POW10_EXP)) return HUGE_VAL;
    if (likely (n < POW10_SMALL)) return _small_powers_of_10[n];
    return _large_powers_of_10[n >> POW10_SHIFT] * _small_powers_of_10[n & POW10_MASK];
  }
};

#endif /* HB_NUMBER_PARSER_HH */
