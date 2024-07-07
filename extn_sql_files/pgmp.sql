/* pgmp -- Module installation SQL script
 *
 * Copyright (C) 2011 Daniele Varrazzo
 *
 * This file is part of the PostgreSQL GMP Module
 *
 * The PostgreSQL GMP Module is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 3 of the License,
 * or (at your option) any later version.
 *
 * The PostgreSQL GMP Module is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the PostgreSQL GMP Module.  If not, see
 * https://www.gnu.org/licenses/.
 */


CREATE OR REPLACE FUNCTION gmp_version()
RETURNS int4
AS '$libdir/pgmp', 'pgmp_gmp_version'
LANGUAGE C IMMUTABLE STRICT ;


--
-- mpz user-defined type
--

CREATE OR REPLACE FUNCTION mpz_in(cstring)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_in'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION mpz_out(mpz)
RETURNS cstring
AS '$libdir/pgmp', 'pmpz_out'
LANGUAGE C IMMUTABLE STRICT ;


CREATE TYPE mpz (
      INPUT = mpz_in
    , OUTPUT = mpz_out
    , INTERNALLENGTH = VARIABLE
    , STORAGE = EXTENDED
    , CATEGORY = 'N'
);


-- Other I/O functions

CREATE OR REPLACE FUNCTION mpz(text, int4)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_in_base'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION text(mpz, int4)
RETURNS cstring
AS '$libdir/pgmp', 'pmpz_out_base'
LANGUAGE C IMMUTABLE STRICT ;



--
-- mpz cast
--

CREATE OR REPLACE FUNCTION mpz(int2)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_from_int2'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (int2 AS mpz)
WITH FUNCTION mpz(int2)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpz(int4)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_from_int4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (int4 AS mpz)
WITH FUNCTION mpz(int4)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpz(int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_from_int8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (int8 AS mpz)
WITH FUNCTION mpz(int8)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpz(float4)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_from_float4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (float4 AS mpz)
WITH FUNCTION mpz(float4)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION mpz(float8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_from_float8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (float8 AS mpz)
WITH FUNCTION mpz(float8)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION mpz(numeric)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_from_numeric'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (numeric AS mpz)
WITH FUNCTION mpz(numeric)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION int8(mpz)
RETURNS int8
AS '$libdir/pgmp', 'pmpz_to_int8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpz AS int8)
WITH FUNCTION int8(mpz)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION int4(mpz)
RETURNS int4
AS '$libdir/pgmp', 'pmpz_to_int4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpz AS int4)
WITH FUNCTION int4(mpz)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION int2(mpz)
RETURNS int2
AS '$libdir/pgmp', 'pmpz_to_int2'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpz AS int2)
WITH FUNCTION int2(mpz)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION float4(mpz)
RETURNS float4
AS '$libdir/pgmp', 'pmpz_to_float4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpz AS float4)
WITH FUNCTION float4(mpz)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION float8(mpz)
RETURNS float8
AS '$libdir/pgmp', 'pmpz_to_float8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpz AS float8)
WITH FUNCTION float8(mpz)
AS ASSIGNMENT;



CREATE CAST (mpz AS numeric) WITH INOUT AS ASSIGNMENT;


--
-- mpz operators
--

CREATE OR REPLACE FUNCTION mpz_uplus(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_uplus'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION mpz_neg(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_neg'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION abs(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_abs'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION sgn(mpz)
RETURNS int4
AS '$libdir/pgmp', 'pmpz_sgn'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION even(mpz)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_even'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION odd(mpz)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_odd'
LANGUAGE C IMMUTABLE STRICT ;


CREATE OPERATOR - (
    RIGHTARG = mpz,
    PROCEDURE = mpz_neg
);

CREATE OPERATOR + (
    RIGHTARG = mpz,
    PROCEDURE = mpz_uplus
);


CREATE OR REPLACE FUNCTION mpz_add(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_add'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR + (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    COMMUTATOR = +,
    PROCEDURE = mpz_add
);


CREATE OR REPLACE FUNCTION mpz_sub(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_sub'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR - (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_sub
);


CREATE OR REPLACE FUNCTION mpz_mul(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_mul'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR * (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    COMMUTATOR = *,
    PROCEDURE = mpz_mul
);


CREATE OR REPLACE FUNCTION mpz_tdiv_q(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_tdiv_q'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR / (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_tdiv_q
);


CREATE OR REPLACE FUNCTION mpz_tdiv_r(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_tdiv_r'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR % (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_tdiv_r
);


CREATE OR REPLACE FUNCTION mpz_cdiv_q(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_cdiv_q'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR +/ (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_cdiv_q
);


CREATE OR REPLACE FUNCTION mpz_cdiv_r(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_cdiv_r'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR +% (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_cdiv_r
);


CREATE OR REPLACE FUNCTION mpz_fdiv_q(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_fdiv_q'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR -/ (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_fdiv_q
);


CREATE OR REPLACE FUNCTION mpz_fdiv_r(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_fdiv_r'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR -% (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_fdiv_r
);


CREATE OR REPLACE FUNCTION mpz_divexact(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_divexact'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR /! (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_divexact
);


CREATE OR REPLACE FUNCTION mpz_mul_2exp(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_mul_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR << (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = mpz_mul_2exp
);


CREATE OR REPLACE FUNCTION mpz_tdiv_q_2exp(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_tdiv_q_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR >> (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = mpz_tdiv_q_2exp
);


CREATE OR REPLACE FUNCTION mpz_tdiv_r_2exp(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_tdiv_r_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR %> (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = mpz_tdiv_r_2exp
);


CREATE OR REPLACE FUNCTION mpz_cdiv_q_2exp(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_cdiv_q_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR +>> (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = mpz_cdiv_q_2exp
);


CREATE OR REPLACE FUNCTION mpz_cdiv_r_2exp(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_cdiv_r_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR +%> (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = mpz_cdiv_r_2exp
);


CREATE OR REPLACE FUNCTION mpz_fdiv_q_2exp(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_fdiv_q_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR ->> (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = mpz_fdiv_q_2exp
);


CREATE OR REPLACE FUNCTION mpz_fdiv_r_2exp(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_fdiv_r_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR -%> (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = mpz_fdiv_r_2exp
);


CREATE OR REPLACE FUNCTION tdiv_qr(mpz, mpz, out q mpz, out r mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_tdiv_qr'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION cdiv_qr(mpz, mpz, out q mpz, out r mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_cdiv_qr'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION fdiv_qr(mpz, mpz, out q mpz, out r mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_fdiv_qr'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION divisible(mpz, mpz)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_divisible'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION divisible_2exp(mpz, int8)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_divisible_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION congruent(mpz, mpz, mpz)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_congruent'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION congruent_2exp(mpz, mpz, int8)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_congruent_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION pow(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_pow_ui'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION powm(mpz, mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_powm'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION mpz_and(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_and'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR & (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_and
);


CREATE OR REPLACE FUNCTION mpz_ior(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_ior'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR | (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_ior
);


CREATE OR REPLACE FUNCTION mpz_xor(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_xor'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR # (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = mpz_xor
);


CREATE OR REPLACE FUNCTION com(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_com'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION popcount(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_popcount'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION hamdist(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_hamdist'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION scan0(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_scan0'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION scan1(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_scan1'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION setbit(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_setbit'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION clrbit(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_clrbit'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION combit(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_combit'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION tstbit(mpz, mpz)
RETURNS int4
AS '$libdir/pgmp', 'pmpz_tstbit'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION gmp_max_bitcnt()
RETURNS mpz
AS '$libdir/pgmp', 'pgmp_max_bitcnt'
LANGUAGE C IMMUTABLE STRICT ;


CREATE OPERATOR /? (
    LEFTARG = mpz,
    RIGHTARG = mpz,
    PROCEDURE = divisible
);

CREATE OPERATOR >>? (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = divisible_2exp
);

CREATE OPERATOR ^ (
    LEFTARG = mpz,
    RIGHTARG = int8,
    PROCEDURE = pow
);


--
-- mpz comparisons
--

CREATE OR REPLACE FUNCTION mpz_eq(mpz, mpz)
RETURNS boolean
AS '$libdir/pgmp', 'pmpz_eq'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR = (
    LEFTARG = mpz
    , RIGHTARG = mpz
    , PROCEDURE = mpz_eq
    , COMMUTATOR = =
    , NEGATOR = <>
    , RESTRICT = eqsel
    , JOIN = eqjoinsel
    , HASHES
    , MERGES
);

CREATE OR REPLACE FUNCTION mpz_ne(mpz, mpz)
RETURNS boolean
AS '$libdir/pgmp', 'pmpz_ne'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR <> (
    LEFTARG = mpz
    , RIGHTARG = mpz
    , PROCEDURE = mpz_ne
    , COMMUTATOR = <>
    , NEGATOR = =
    , RESTRICT = neqsel
    , JOIN = neqjoinsel
);

CREATE OR REPLACE FUNCTION mpz_gt(mpz, mpz)
RETURNS boolean
AS '$libdir/pgmp', 'pmpz_gt'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR > (
    LEFTARG = mpz
    , RIGHTARG = mpz
    , PROCEDURE = mpz_gt
    , COMMUTATOR = <
    , NEGATOR = <=
    , RESTRICT = scalargtsel
    , JOIN = scalargtjoinsel
);


CREATE OR REPLACE FUNCTION mpz_ge(mpz, mpz)
RETURNS boolean
AS '$libdir/pgmp', 'pmpz_ge'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR >= (
    LEFTARG = mpz
    , RIGHTARG = mpz
    , PROCEDURE = mpz_ge
    , COMMUTATOR = <=
    , NEGATOR = <
    , RESTRICT = scalargtsel
    , JOIN = scalargtjoinsel
);


CREATE OR REPLACE FUNCTION mpz_lt(mpz, mpz)
RETURNS boolean
AS '$libdir/pgmp', 'pmpz_lt'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR < (
    LEFTARG = mpz
    , RIGHTARG = mpz
    , PROCEDURE = mpz_lt
    , COMMUTATOR = >
    , NEGATOR = >=
    , RESTRICT = scalarltsel
    , JOIN = scalarltjoinsel
);


CREATE OR REPLACE FUNCTION mpz_le(mpz, mpz)
RETURNS boolean
AS '$libdir/pgmp', 'pmpz_le'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR <= (
    LEFTARG = mpz
    , RIGHTARG = mpz
    , PROCEDURE = mpz_le
    , COMMUTATOR = >=
    , NEGATOR = >
    , RESTRICT = scalarltsel
    , JOIN = scalarltjoinsel
);




--
-- mpz indexes
--

CREATE OR REPLACE FUNCTION mpz_cmp(mpz, mpz)
RETURNS integer
AS '$libdir/pgmp', 'pmpz_cmp'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR CLASS mpz_ops
DEFAULT FOR TYPE mpz USING btree AS
    OPERATOR    1   <   ,
    OPERATOR    2   <=  ,
    OPERATOR    3   =   ,
    OPERATOR    4   >=  ,
    OPERATOR    5   >   ,
    FUNCTION    1   mpz_cmp(mpz, mpz)
    ;

CREATE OR REPLACE FUNCTION mpz_hash(mpz)
RETURNS integer
AS '$libdir/pgmp', 'pmpz_hash'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR CLASS mpz_ops
DEFAULT FOR TYPE mpz USING hash AS
    OPERATOR    1   =   ,
    FUNCTION    1   mpz_hash(mpz)
    ;

-- TODO: OPERATOR FAMILY?

-- mpz functions

CREATE OR REPLACE FUNCTION sqrt(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_sqrt'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION root(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_root'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION perfect_power(mpz)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_perfect_power'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION perfect_square(mpz)
RETURNS bool
AS '$libdir/pgmp', 'pmpz_perfect_square'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION rootrem(mpz, int8, out root mpz, out rem mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_rootrem'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION sqrtrem(mpz, out root mpz, out rem mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_sqrtrem'
LANGUAGE C IMMUTABLE STRICT;



--
-- Number Theoretic Functions
--

CREATE OR REPLACE FUNCTION probab_prime(mpz, int4)
RETURNS int4
AS '$libdir/pgmp', 'pmpz_probab_prime_p'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION nextprime(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_nextprime'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION gcd(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_gcd'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION lcm(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_lcm'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION invert(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_invert'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION jacobi(mpz, mpz)
RETURNS int4
AS '$libdir/pgmp', 'pmpz_jacobi'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION legendre(mpz, mpz)
RETURNS int4
AS '$libdir/pgmp', 'pmpz_legendre'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION kronecker(mpz, mpz)
RETURNS int4
AS '$libdir/pgmp', 'pmpz_kronecker'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION remove(mpz, mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_remove'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION fac(int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_fac_ui'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION bin(mpz, int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_bin_ui'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION fib(int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_fib_ui'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION lucnum(int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_lucnum_ui'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION gcdext(mpz, mpz, out g mpz, out s mpz, out t mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_gcdext'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION fib2(int8, out fn mpz, out fnsub1 mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_fib2_ui'
LANGUAGE C IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION lucnum2(int8, out ln mpz, out lnsub1 mpz)
RETURNS RECORD
AS '$libdir/pgmp', 'pmpz_lucnum2_ui'
LANGUAGE C IMMUTABLE STRICT;



--
-- Random numbers
--

CREATE OR REPLACE FUNCTION randinit()
RETURNS void
AS '$libdir/pgmp', 'pgmp_randinit_default'
LANGUAGE C VOLATILE STRICT ;

CREATE OR REPLACE FUNCTION randinit_mt()
RETURNS void
AS '$libdir/pgmp', 'pgmp_randinit_mt'
LANGUAGE C VOLATILE STRICT ;

CREATE OR REPLACE FUNCTION randinit_lc_2exp(mpz, int8, int8)
RETURNS void
AS '$libdir/pgmp', 'pgmp_randinit_lc_2exp'
LANGUAGE C VOLATILE STRICT ;

CREATE OR REPLACE FUNCTION randinit_lc_2exp_size(int8)
RETURNS void
AS '$libdir/pgmp', 'pgmp_randinit_lc_2exp_size'
LANGUAGE C VOLATILE STRICT ;

CREATE OR REPLACE FUNCTION randseed(mpz)
RETURNS void
AS '$libdir/pgmp', 'pgmp_randseed'
LANGUAGE C VOLATILE STRICT ;

CREATE OR REPLACE FUNCTION urandomb(int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_urandomb'
LANGUAGE C VOLATILE STRICT ;

CREATE OR REPLACE FUNCTION urandomm(mpz)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_urandomm'
LANGUAGE C VOLATILE STRICT ;

CREATE OR REPLACE FUNCTION rrandomb(int8)
RETURNS mpz
AS '$libdir/pgmp', 'pmpz_rrandomb'
LANGUAGE C VOLATILE STRICT ;



--
-- Aggregation functions
--

CREATE OR REPLACE FUNCTION _mpz_from_agg(internal)
RETURNS mpz
AS '$libdir/pgmp', '_pmpz_from_agg'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION _mpz_agg_add(internal, mpz)
RETURNS internal
AS '$libdir/pgmp', '_pmpz_agg_add'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE sum(mpz)
(
      SFUNC = _mpz_agg_add
    , STYPE = internal
    , FINALFUNC = _mpz_from_agg
);

CREATE OR REPLACE FUNCTION _mpz_agg_mul(internal, mpz)
RETURNS internal
AS '$libdir/pgmp', '_pmpz_agg_mul'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE prod(mpz)
(
      SFUNC = _mpz_agg_mul
    , STYPE = internal
    , FINALFUNC = _mpz_from_agg
);

CREATE OR REPLACE FUNCTION _mpz_agg_max(internal, mpz)
RETURNS internal
AS '$libdir/pgmp', '_pmpz_agg_max'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE max(mpz)
(
      SFUNC = _mpz_agg_max
    , STYPE = internal
    , FINALFUNC = _mpz_from_agg
    , SORTOP = >
);

CREATE OR REPLACE FUNCTION _mpz_agg_min(internal, mpz)
RETURNS internal
AS '$libdir/pgmp', '_pmpz_agg_min'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE min(mpz)
(
      SFUNC = _mpz_agg_min
    , STYPE = internal
    , FINALFUNC = _mpz_from_agg
    , SORTOP = <
);

CREATE OR REPLACE FUNCTION _mpz_agg_and(internal, mpz)
RETURNS internal
AS '$libdir/pgmp', '_pmpz_agg_and'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE bit_and(mpz)
(
      SFUNC = _mpz_agg_and
    , STYPE = internal
    , FINALFUNC = _mpz_from_agg
);

CREATE OR REPLACE FUNCTION _mpz_agg_ior(internal, mpz)
RETURNS internal
AS '$libdir/pgmp', '_pmpz_agg_ior'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE bit_or(mpz)
(
      SFUNC = _mpz_agg_ior
    , STYPE = internal
    , FINALFUNC = _mpz_from_agg
);

CREATE OR REPLACE FUNCTION _mpz_agg_xor(internal, mpz)
RETURNS internal
AS '$libdir/pgmp', '_pmpz_agg_xor'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE bit_xor(mpz)
(
      SFUNC = _mpz_agg_xor
    , STYPE = internal
    , FINALFUNC = _mpz_from_agg
);



--
-- mpq user-defined type
--

CREATE OR REPLACE FUNCTION mpq_in(cstring)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_in'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION mpq_out(mpq)
RETURNS cstring
AS '$libdir/pgmp', 'pmpq_out'
LANGUAGE C IMMUTABLE STRICT ;


CREATE TYPE mpq (
      INPUT = mpq_in
    , OUTPUT = mpq_out
    , INTERNALLENGTH = VARIABLE
    , STORAGE = EXTENDED
    , CATEGORY = 'N'
);

CREATE OR REPLACE FUNCTION mpq(text, int4)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_in_base'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION text(mpq, int4)
RETURNS cstring
AS '$libdir/pgmp', 'pmpq_out_base'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION mpq(mpz, mpz)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_mpz_mpz'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION num(mpq)
RETURNS mpz
AS '$libdir/pgmp', 'pmpq_num'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION den(mpq)
RETURNS mpz
AS '$libdir/pgmp', 'pmpq_den'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION mpq(int2)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_from_int2'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (int2 AS mpq)
WITH FUNCTION mpq(int2)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpq(int4)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_from_int4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (int4 AS mpq)
WITH FUNCTION mpq(int4)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpq(int8)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_from_int8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (int8 AS mpq)
WITH FUNCTION mpq(int8)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpq(float4)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_from_float4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (float4 AS mpq)
WITH FUNCTION mpq(float4)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpq(float8)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_from_float8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (float8 AS mpq)
WITH FUNCTION mpq(float8)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpq(numeric)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_from_numeric'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (numeric AS mpq)
WITH FUNCTION mpq(numeric)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION mpq(mpz)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_from_mpz'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpz AS mpq)
WITH FUNCTION mpq(mpz)
AS IMPLICIT;


CREATE OR REPLACE FUNCTION int2(mpq)
RETURNS int2
AS '$libdir/pgmp', 'pmpq_to_int2'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpq AS int2)
WITH FUNCTION int2(mpq)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION int4(mpq)
RETURNS int4
AS '$libdir/pgmp', 'pmpq_to_int4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpq AS int4)
WITH FUNCTION int4(mpq)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION int8(mpq)
RETURNS int8
AS '$libdir/pgmp', 'pmpq_to_int8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpq AS int8)
WITH FUNCTION int8(mpq)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION float4(mpq)
RETURNS float4
AS '$libdir/pgmp', 'pmpq_to_float4'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpq AS float4)
WITH FUNCTION float4(mpq)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION float8(mpq)
RETURNS float8
AS '$libdir/pgmp', 'pmpq_to_float8'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpq AS float8)
WITH FUNCTION float8(mpq)
AS ASSIGNMENT;


CREATE OR REPLACE FUNCTION mpz(mpq)
RETURNS mpz
AS '$libdir/pgmp', 'pmpq_to_mpz'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpq AS mpz)
WITH FUNCTION mpz(mpq)
AS ASSIGNMENT;



CREATE OR REPLACE FUNCTION mpq_to_numeric(mpq, int4)
RETURNS numeric
AS '$libdir/pgmp', 'pmpq_to_numeric'
LANGUAGE C IMMUTABLE STRICT ;

CREATE CAST (mpq AS numeric)
WITH FUNCTION mpq_to_numeric(mpq, int4)
AS ASSIGNMENT;


--
-- mpq operators
--

CREATE OR REPLACE FUNCTION mpq_uplus(mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_uplus'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION mpq_neg(mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_neg'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION abs(mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_abs'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION inv(mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_inv'
LANGUAGE C IMMUTABLE STRICT ;


CREATE OPERATOR - (
    RIGHTARG = mpq,
    PROCEDURE = mpq_neg
);

CREATE OPERATOR + (
    RIGHTARG = mpq,
    PROCEDURE = mpq_uplus
);


CREATE OR REPLACE FUNCTION mpq_add(mpq, mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_add'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR + (
    LEFTARG = mpq,
    RIGHTARG = mpq,
    COMMUTATOR = +,
    PROCEDURE = mpq_add
);


CREATE OR REPLACE FUNCTION mpq_sub(mpq, mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_sub'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR - (
    LEFTARG = mpq,
    RIGHTARG = mpq,
    PROCEDURE = mpq_sub
);


CREATE OR REPLACE FUNCTION mpq_mul(mpq, mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_mul'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR * (
    LEFTARG = mpq,
    RIGHTARG = mpq,
    COMMUTATOR = *,
    PROCEDURE = mpq_mul
);


CREATE OR REPLACE FUNCTION mpq_div(mpq, mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_div'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR / (
    LEFTARG = mpq,
    RIGHTARG = mpq,
    PROCEDURE = mpq_div
);


CREATE OR REPLACE FUNCTION mpq_mul_2exp(mpq, int8)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_mul_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR << (
    LEFTARG = mpq,
    RIGHTARG = int8,
    PROCEDURE = mpq_mul_2exp
);


CREATE OR REPLACE FUNCTION mpq_div_2exp(mpq, int8)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_div_2exp'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR >> (
    LEFTARG = mpq,
    RIGHTARG = int8,
    PROCEDURE = mpq_div_2exp
);


CREATE OR REPLACE FUNCTION limit_den(mpq)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_limit_den'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION limit_den(mpq, mpz)
RETURNS mpq
AS '$libdir/pgmp', 'pmpq_limit_den'
LANGUAGE C IMMUTABLE STRICT ;



--
-- mpq comparisons
--

CREATE OR REPLACE FUNCTION mpq_eq(mpq, mpq)
RETURNS boolean
AS '$libdir/pgmp', 'pmpq_eq'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR = (
    LEFTARG = mpq
    , RIGHTARG = mpq
    , PROCEDURE = mpq_eq
    , COMMUTATOR = =
    , NEGATOR = <>
    , RESTRICT = eqsel
    , JOIN = eqjoinsel
    , HASHES
    , MERGES
);

CREATE OR REPLACE FUNCTION mpq_ne(mpq, mpq)
RETURNS boolean
AS '$libdir/pgmp', 'pmpq_ne'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR <> (
    LEFTARG = mpq
    , RIGHTARG = mpq
    , PROCEDURE = mpq_ne
    , COMMUTATOR = <>
    , NEGATOR = =
    , RESTRICT = neqsel
    , JOIN = neqjoinsel
);

CREATE OR REPLACE FUNCTION mpq_gt(mpq, mpq)
RETURNS boolean
AS '$libdir/pgmp', 'pmpq_gt'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR > (
    LEFTARG = mpq
    , RIGHTARG = mpq
    , PROCEDURE = mpq_gt
    , COMMUTATOR = <
    , NEGATOR = <=
    , RESTRICT = scalargtsel
    , JOIN = scalargtjoinsel
);


CREATE OR REPLACE FUNCTION mpq_ge(mpq, mpq)
RETURNS boolean
AS '$libdir/pgmp', 'pmpq_ge'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR >= (
    LEFTARG = mpq
    , RIGHTARG = mpq
    , PROCEDURE = mpq_ge
    , COMMUTATOR = <=
    , NEGATOR = <
    , RESTRICT = scalargtsel
    , JOIN = scalargtjoinsel
);


CREATE OR REPLACE FUNCTION mpq_lt(mpq, mpq)
RETURNS boolean
AS '$libdir/pgmp', 'pmpq_lt'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR < (
    LEFTARG = mpq
    , RIGHTARG = mpq
    , PROCEDURE = mpq_lt
    , COMMUTATOR = >
    , NEGATOR = >=
    , RESTRICT = scalarltsel
    , JOIN = scalarltjoinsel
);


CREATE OR REPLACE FUNCTION mpq_le(mpq, mpq)
RETURNS boolean
AS '$libdir/pgmp', 'pmpq_le'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OPERATOR <= (
    LEFTARG = mpq
    , RIGHTARG = mpq
    , PROCEDURE = mpq_le
    , COMMUTATOR = >=
    , NEGATOR = >
    , RESTRICT = scalarltsel
    , JOIN = scalarltjoinsel
);




--
-- mpq indexes
--

CREATE OR REPLACE FUNCTION mpq_cmp(mpq, mpq)
RETURNS integer
AS '$libdir/pgmp', 'pmpq_cmp'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR CLASS mpq_ops
DEFAULT FOR TYPE mpq USING btree AS
    OPERATOR    1   <   ,
    OPERATOR    2   <=  ,
    OPERATOR    3   =   ,
    OPERATOR    4   >=  ,
    OPERATOR    5   >   ,
    FUNCTION    1   mpq_cmp(mpq, mpq)
    ;


CREATE OR REPLACE FUNCTION mpq_hash(mpq)
RETURNS integer
AS '$libdir/pgmp', 'pmpq_hash'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR CLASS mpq_ops
DEFAULT FOR TYPE mpq USING hash AS
    OPERATOR    1   =   ,
    FUNCTION    1   mpq_hash(mpq)
    ;

-- TODO: OPERATOR FAMILY?


--
-- Aggregation functions
--

CREATE OR REPLACE FUNCTION _mpq_from_agg(internal)
RETURNS mpq
AS '$libdir/pgmp', '_pmpq_from_agg'
LANGUAGE C IMMUTABLE STRICT ;

CREATE OR REPLACE FUNCTION _mpq_agg_add(internal, mpq)
RETURNS internal
AS '$libdir/pgmp', '_pmpq_agg_add'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE sum(mpq)
(
      SFUNC = _mpq_agg_add
    , STYPE = internal
    , FINALFUNC = _mpq_from_agg
);

CREATE OR REPLACE FUNCTION _mpq_agg_mul(internal, mpq)
RETURNS internal
AS '$libdir/pgmp', '_pmpq_agg_mul'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE prod(mpq)
(
      SFUNC = _mpq_agg_mul
    , STYPE = internal
    , FINALFUNC = _mpq_from_agg
);

CREATE OR REPLACE FUNCTION _mpq_agg_max(internal, mpq)
RETURNS internal
AS '$libdir/pgmp', '_pmpq_agg_max'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE max(mpq)
(
      SFUNC = _mpq_agg_max
    , STYPE = internal
    , FINALFUNC = _mpq_from_agg
    , SORTOP = >
);

CREATE OR REPLACE FUNCTION _mpq_agg_min(internal, mpq)
RETURNS internal
AS '$libdir/pgmp', '_pmpq_agg_min'
LANGUAGE C IMMUTABLE  ;

CREATE AGGREGATE min(mpq)
(
      SFUNC = _mpq_agg_min
    , STYPE = internal
    , FINALFUNC = _mpq_from_agg
    , SORTOP = <
);


