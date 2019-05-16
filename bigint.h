#ifndef __JKJ_GRISU_EXACT_BIGINT__
#define __JKJ_GRISU_EXACT_BIGINT__

#include "grisu_exact.h"
#include <algorithm>
#include <cassert>

namespace jkj {
	namespace grisu_exact_detail {
		////////////////////////////////////////////////////////////////////////////////////////
		// This file is only used for cache generation, and need not be included for real use
		////////////////////////////////////////////////////////////////////////////////////////

		constexpr std::size_t log2p1(std::uint64_t x) noexcept {
			// C++20 std::log2p1 is not yet supported
			//return std::log2p1(x);

			std::size_t ret = 0;
			auto inspect = [&x, &ret](int shft) {
				if ((x >> shft) != 0) {
					x >>= shft;
					ret += shft;
				}
			};

			inspect(32);
			inspect(16);
			inspect(8);
			inspect(4);
			inspect(2);
			inspect(1);

			return ret + x;
		}

		struct bigint_base {
			using element_type = std::uint64_t;
			static constexpr std::size_t element_number_of_bits =
				sizeof(element_type) * std::numeric_limits<unsigned char>::digits;

			struct leading_one_pos_t {
				std::size_t		element_pos;
				std::size_t		bit_pos;		// 1 ~ element_number_of_bits
			};
		};

		template <std::size_t array_size>
		struct bigint_impl;
		
		template <std::size_t max_bits>
		using bigint = bigint_impl<(max_bits + bigint_base::element_number_of_bits - 1) /
			bigint_base::element_number_of_bits>;

		template <std::size_t array_size_>
		struct bigint_impl : public bigint_base {
			static constexpr std::size_t array_size = array_size_;

			element_type		elements[array_size];
			leading_one_pos_t	leading_one_pos;

			bigint_impl() = default;

			bigint_impl(element_type x) :
				leading_one_pos{ 0, log2p1(x) }
			{
				elements[0] = x;
				std::fill_n(std::begin(elements) + 1, array_size - 1, 0);
			}

			static bigint_impl power_of_2(std::size_t exp) noexcept {
				assert(exp < array_size * element_number_of_bits);
				bigint_impl ret;
				std::fill_n(std::begin(ret.elements), array_size, 0);

				ret.leading_one_pos.element_pos = exp / element_number_of_bits;
				ret.leading_one_pos.bit_pos = exp % element_number_of_bits + 1;

				ret.elements[ret.leading_one_pos.element_pos] =
					(element_type(1) << (ret.leading_one_pos.bit_pos - 1));

				return ret;
			}

			// Repeat multiplying 2 until the number becomes bigger than or equal to the given number
			// Returns the number of multiplications
			// Precondition: n should be bigger than or equal to the current number
			// Note that this function need not require &n != this
			std::size_t multiply_2_until(bigint_impl const& n) & {
				assert(leading_one_pos.bit_pos != 0);

				std::size_t number_of_multiplications = 0;

				// Perform left-shift to match the leading-1 position
				// Perform element-wise shift first
				assert(leading_one_pos.element_pos <= n.leading_one_pos.element_pos);
				auto element_pos_offset = n.leading_one_pos.element_pos - leading_one_pos.element_pos;
				if (element_pos_offset > 0) {
					number_of_multiplications = element_pos_offset * element_number_of_bits;

					std::move_backward(elements.begin(),
						std::begin(elements) + leading_one_pos.element_pos + 1,
						std::begin(elements) + n.leading_one_pos.element_pos + 1);

					std::fill_n(elements.begin(), element_pos_offset, 0);

					leading_one_pos.element_pos += element_pos_offset;
				}
				// And then perform bit-wise shift
				auto bit_pos_offset = std::ptrdiff_t(n.leading_one_pos.bit_pos) -
					std::ptrdiff_t(leading_one_pos.bit_pos);
				number_of_multiplications += bit_pos_offset;
				if (bit_pos_offset > 0) {
					// Left-shfit
					auto shft = std::size_t(bit_pos_offset);
					auto remaining_bits = element_number_of_bits - shft;
					for (auto idx = leading_one_pos.element_pos; idx > 0; --idx) {
						auto bits_to_transfer = elements[idx - 1] >> remaining_bits;

						elements[idx] <<= shft;
						elements[idx] |= bits_to_transfer;
					}
					elements[0] <<= shft;
				}
				else if (bit_pos_offset < 0) {
					// Right-shift
					auto shft = std::size_t(-bit_pos_offset);
					auto remaining_bits = element_number_of_bits - shft;
					elements[0] >>= shft;
					for (std::size_t idx = 1; idx <= leading_one_pos.element_pos; ++idx) {
						auto bits_to_transfer = elements[idx] << remaining_bits;

						elements[idx - 1] |= bits_to_transfer;
						elements[idx] >>= shft;
					}
				}
				leading_one_pos.bit_pos = n.leading_one_pos.bit_pos;

				// Compare the shifted number with the given number
				bool is_bigger_than_or_equal_to = true;
				for (auto idx = std::ptrdiff_t(leading_one_pos.element_pos); idx >= 0; --idx) {
					if (elements[idx] > n.elements[idx])
						break;
					else if (elements[idx] < n.elements[idx]) {
						is_bigger_than_or_equal_to = false;
						break;
					}
				}

				// If our number is still less
				if (!is_bigger_than_or_equal_to) {
					// Shift one more bit
					++number_of_multiplications;
					if (leading_one_pos.bit_pos == element_number_of_bits) {
						leading_one_pos.bit_pos = 1;
						++leading_one_pos.element_pos;
						assert(leading_one_pos.element_pos != array_size);
					}
					else
						++leading_one_pos.bit_pos;

					constexpr auto remaining_bits = element_number_of_bits - 1;
					for (auto idx = leading_one_pos.element_pos; idx > 0; --idx) {
						elements[idx] <<= 1;
						elements[idx] |= (elements[idx - 1] >> remaining_bits);
					}
					elements[0] <<= 1;
				}

				return number_of_multiplications;
			}

			// Multiply 5 to the current number
			void multiply_5() & {
				assert(leading_one_pos.bit_pos != 0);

				decltype(elements) times_4;
				leading_one_pos_t times_4_leading_one_pos{
					leading_one_pos.element_pos,
					leading_one_pos.bit_pos + 2
				};

				// Fix leading-1 position
				if (times_4_leading_one_pos.bit_pos > element_number_of_bits) {
					assert(leading_one_pos.element_pos + 1 != array_size);

					times_4_leading_one_pos.bit_pos -= element_number_of_bits;
					++times_4_leading_one_pos.element_pos;
				}

				// Calculate times_4
				for (auto idx = times_4_leading_one_pos.element_pos; idx > 0; --idx) {
					times_4[idx] = elements[idx] << 2;
					times_4[idx] |= (elements[idx - 1] >> (element_number_of_bits - 2));
				}
				times_4[0] = elements[0] << 2;

				// Add
				elements[0] += times_4[0];
				unsigned int carry = (elements[0] < times_4[0]) ? 1 : 0;

				for (std::size_t idx = 1; idx <= times_4_leading_one_pos.element_pos; ++idx) {
					auto copied_with_carry = elements[idx] + carry;
					unsigned int first_carry = (copied_with_carry < elements[idx]) ? 1 : 0;

					elements[idx] = copied_with_carry + times_4[idx];
					carry = first_carry | ((elements[idx] < times_4[idx]) ? 1 : 0);
				}

				if (carry != 0) {
					leading_one_pos.element_pos = times_4_leading_one_pos.element_pos + 1;
					leading_one_pos.bit_pos = 1;

					elements[leading_one_pos.element_pos] = 1;
				}
				else {
					leading_one_pos.element_pos = times_4_leading_one_pos.element_pos;

					// The expression "element_type(1) << times_4_leading_one_pos.bit_pos"
					// is UB if times_4_leading_one_pos.bit_pos == element_number_of_bits,
					// so we have to use the following instead:
					auto threshold = element_type(-1) >>
						(element_number_of_bits - times_4_leading_one_pos.bit_pos);
					if (elements[leading_one_pos.element_pos] > threshold)
						leading_one_pos.bit_pos = times_4_leading_one_pos.bit_pos + 1;
					else
						leading_one_pos.bit_pos = times_4_leading_one_pos.bit_pos;
				}

				assert(leading_one_pos.element_pos != array_size);
				assert(leading_one_pos.bit_pos <= element_number_of_bits);
			}

			// Increment
			constexpr bigint_impl& operator++() & {
				std::size_t idx = 0;
				unsigned int carry;
				do {
					assert(idx < array_size);
					carry = (++elements[idx++] == 0) ? 1 : 0;
				} while (carry != 0);

				if (idx == leading_one_pos.element_pos + 1) {
					leading_one_pos.bit_pos = log2p1(elements[leading_one_pos.element_pos]);
				}
				else if (idx > leading_one_pos.element_pos + 1) {
					++leading_one_pos.element_pos;
					leading_one_pos.bit_pos = 1;
				}

				return *this;
			}

			// Left-shift
			// Precondition: shft + leading_one_pos.bit_pos <= element_number_of_bits,
			//               0 < shft < element_number_of_bits.
			constexpr bigint_impl& operator<<=(std::size_t shft) & {
				assert(shft + leading_one_pos.bit_pos <= element_number_of_bits);
				assert(shft > 0 && shft < element_number_of_bits);
				leading_one_pos.bit_pos += shft;

				for (std::size_t idx = leading_one_pos.element_pos; idx > 0; --idx) {
					elements[idx] <<= shft;
					elements[idx] |= (elements[idx - 1] >> (element_number_of_bits - shft));
				}
				elements[0] <<= shft;

				return *this;
			}

			// Right-shift
			// Precondition: leading_one_pos.bit_pos >= shft,
			//               0 < shft < element_number_of_bits.
			constexpr bigint_impl& operator>>=(std::size_t shft) & {
				assert(leading_one_pos.bit_pos >= shft);
				assert(shft > 0 && shft < element_number_of_bits);
				leading_one_pos.bit_pos -= shft;

				for (std::size_t idx = 0; idx < leading_one_pos.element_pos; ++idx) {
					elements[idx] >>= shft;
					elements[idx] |= (elements[idx + 1] << (element_number_of_bits - shft));
				}
				elements[leading_one_pos.element_pos] >>= shft;

				return *this;
			}

		private:
			constexpr int compare_common(bigint_impl const& n) const noexcept {
				if (leading_one_pos.element_pos < n.leading_one_pos.element_pos)
					return 1;
				else if (leading_one_pos.element_pos > n.leading_one_pos.element_pos)
					return -1;

				for (std::size_t idx = leading_one_pos.element_pos; idx > 0; --idx) {
					if (elements[idx] < n.elements[idx])
						return 1;
					else if (elements[idx] > n.elements[idx])
						return -1;
				}

				return 0;
			}

		public:
			constexpr bool operator<(bigint_impl const& n) const noexcept {
				auto c = compare_common(n);
				if (c > 0)
					return true;
				else if (c < 0)
					return false;

				if (elements[0] < n.elements[0])
					return true;
				else
					return false;
			}
			constexpr bool operator<=(bigint_impl const& n) const noexcept {
				auto c = compare_common(n);
				if (c > 0)
					return true;
				else if (c < 0)
					return false;

				if (elements[0] <= n.elements[0])
					return true;
				else
					return false;
			}
			constexpr bool operator>(bigint_impl const& n) const noexcept {
				auto c = compare_common(n);
				if (c < 0)
					return true;
				else if (c > 0)
					return false;

				if (elements[0] > n.elements[0])
					return true;
				else
					return false;
			}
			constexpr bool operator>=(bigint_impl const& n) const noexcept {
				auto c = compare_common(n);
				if (c < 0)
					return true;
				else if (c > 0)
					return false;

				if (elements[0] >= n.elements[0])
					return true;
				else
					return false;
			}
			constexpr bool operator==(bigint_impl const& n) const noexcept {
				auto c = compare_common(n);
				if (c != 0)
					return false;

				return elements[0] == n.elements[0];
			}
			constexpr bool operator!=(bigint_impl const& n) const noexcept {
				auto c = compare_common(n);
				if (c != 0)
					return true;

				return elements[0] != n.elements[0];
			}

			constexpr bool operator<(element_type n) const noexcept {
				if (leading_one_pos.element_pos != 0)
					return false;
				return elements[0] < n;
			}
			constexpr bool operator<=(element_type n) const noexcept {
				if (leading_one_pos.element_pos != 0)
					return false;
				return elements[0] <= n;
			}
			constexpr bool operator>(element_type n) const noexcept {
				if (leading_one_pos.element_pos != 0)
					return true;
				return elements[0] > n;
			}
			constexpr bool operator>=(element_type n) const noexcept {
				if (leading_one_pos.element_pos != 0)
					return true;
				return elements[0] >= n;
			}
			constexpr bool operator==(element_type n) const noexcept {
				if (leading_one_pos.element_pos != 0)
					return false;
				return elements[0] == n;
			}
			constexpr bool operator!=(element_type n) const noexcept {
				if (leading_one_pos.element_pos != 0)
					return true;
				return elements[0] != n;
			}

			// Add another number
			constexpr bigint_impl& operator+=(bigint_impl const& n) & {
				// This auxiliary variable is necessary to avoid the problem of a += a
				auto n_element = n.elements[0];

				elements[0] += n_element;
				unsigned int carry = (elements[0] < n_element) ? 1 : 0;

				std::size_t idx = 1;
				auto add_common = [&](std::size_t min_idx) {
					for (; idx <= min_idx; ++idx) {
						auto with_carry = elements[idx] + carry;
						unsigned int first_carry = (with_carry < elements[idx]) ? 1 : 0;

						n_element = n.elements[idx];
						elements[idx] = with_carry + n_element;
						carry = first_carry | ((elements[idx] < n_element) ? 1 : 0);
					}
				};
				auto set_leading_one_pos = [&](std::size_t max_idx) {
					if (carry != 0) {
						assert(max_idx + 1 < array_size);
						leading_one_pos.element_pos = max_idx + 1;
						leading_one_pos.bit_pos = 1;
						elements[max_idx + 1] = 1;
					}
					else {
						leading_one_pos.element_pos = max_idx;
						leading_one_pos.bit_pos = log2p1(elements[max_idx]);
					}
				};

				if (leading_one_pos.element_pos < n.leading_one_pos.element_pos) {
					add_common(leading_one_pos.element_pos);

					for (; idx <= n.leading_one_pos.element_pos; ++idx) {
						elements[idx] = n.elements[idx] + carry;
						carry = elements[idx] < carry ? 1 : 0;
					}

					set_leading_one_pos(n.leading_one_pos.element_pos);
				}
				else {
					add_common(n.leading_one_pos.element_pos);

					for (; idx <= leading_one_pos.element_pos; ++idx) {
						elements[idx] += carry;
						carry = elements[idx] < carry ? 1 : 0;
					}

					set_leading_one_pos(leading_one_pos.element_pos);
				}

				return *this;
			}

			constexpr bigint_impl& operator+=(element_type n) & {
				elements[0] += n;
				unsigned int carry = (elements[0] < n) ? 1 : 0;

				for (std::size_t idx = 1; idx <= leading_one_pos.element_pos; ++idx) {
					elements[idx] += carry;
					carry = elements[idx] < carry ? 1 : 0;
				}

				if (carry != 0) {
					assert(leading_one_pos.element_pos + 1 < array_size);
					++leading_one_pos.element_pos;
					leading_one_pos.bit_pos = 1;
					elements[leading_one_pos.element_pos] = 1;
				}

				return *this;
			}

			// Subtract another number
			// Precondition: n should be strictly smaller than or equal to the current number
			constexpr bigint_impl& operator-=(bigint_impl const& n) & {
				unsigned int carry = elements[0] < n.elements[0] ? 1 : 0;
				elements[0] -= n.elements[0];

				std::size_t idx = 1;
				for (; idx <= n.leading_one_pos.element_pos; ++idx) {
					auto with_carry = n.elements[idx] + carry;
					unsigned int first_carry = (with_carry < carry) ? 1 : 0;

					carry = first_carry | ((elements[idx] < with_carry) ? 1 : 0);
					elements[idx] -= with_carry;
				}

				if (carry != 0) {
					while (elements[idx] == 0) {
						elements[idx++] = element_type(-1);
						assert(idx < array_size);
					}
					--elements[idx];
				}

				// Find the new leading-1 position
				for (idx = leading_one_pos.element_pos; idx > 0; --idx) {
					if (elements[idx] != 0)
						break;
				}

				leading_one_pos.element_pos = idx;
				leading_one_pos.bit_pos = log2p1(elements[idx]);

				return *this;
			}

			// Multiply a single element
			bigint_impl& operator*=(element_type n) & {
				element_type carry = 0;
				for (std::size_t idx = 0; idx <= leading_one_pos.element_pos; ++idx) {
					auto mul = umul128(elements[idx], n);
					elements[idx] = mul.low() + carry;
					carry = mul.high() + (elements[idx] < mul.low() ? 1 : 0);
				}
				if (carry != 0) {
					assert(leading_one_pos.element_pos < array_size - 1);
					elements[++leading_one_pos.element_pos] = carry;
					leading_one_pos.bit_pos = log2p1(carry);
				}
				else {
					leading_one_pos.bit_pos = log2p1(elements[leading_one_pos.element_pos]);
				}
				return *this;
			}

			// Multiplication
			friend bigint_impl operator*(bigint_impl const& x, bigint_impl const& y) {
				// Leaky overflow check
				assert(x.leading_one_pos.element_pos + y.leading_one_pos.element_pos < array_size);

				bigint_impl temp, result;
				temp.leading_one_pos.element_pos = 0;

				auto calculate_single = [&x](element_type n, bigint_impl& result) {
					bigint_base::element_type carry = 0;
					for (std::size_t idx = 0; idx <= x.leading_one_pos.element_pos; ++idx) {
						auto mul = umul128(x.elements[idx], n);
						result.elements[idx] = mul.low() + carry;
						carry = mul.high() + (result.elements[idx] < mul.low() ? 1 : 0);
					}
					if (carry != 0) {
						result.leading_one_pos.element_pos = x.leading_one_pos.element_pos + 1;
						result.elements[result.leading_one_pos.element_pos] = carry;
					}
					else {
						result.leading_one_pos.element_pos = x.leading_one_pos.element_pos;
					}
				};

				// First iteration
				calculate_single(y.elements[0], result);

				// Remaining iterations
				for (std::size_t i = 1; i <= y.leading_one_pos.element_pos; ++i) {
					calculate_single(y.elements[i], temp);

					// Accumulate
					result.elements[i] += temp.elements[0];
					unsigned int carry = result.elements[i] < temp.elements[0] ? 1 : 0;
					for (std::size_t j = 1; j <= temp.leading_one_pos.element_pos; ++j) {
						auto with_carry = temp.elements[j] + carry;
						auto first_carry = with_carry < temp.elements[j] ? 1 : 0;

						result.elements[i + j] += with_carry;
						carry = first_carry | ((result.elements[i + j] < with_carry) ? 1 : 0);
					}

					assert(carry == 0);
				}

				result.leading_one_pos.element_pos = y.leading_one_pos.element_pos +
					temp.leading_one_pos.element_pos;
				result.leading_one_pos.bit_pos =
					log2p1(result.elements[result.leading_one_pos.element_pos]);

				std::fill(std::begin(result.elements) + result.leading_one_pos.element_pos + 1,
					std::end(result.elements), 0);

				return result;
			}

			// Perform long division
			// *this becomes the remainder, returns the quotient
			// Precondition: n != 0
			bigint_impl long_division(bigint_impl const& n) & {
				bigint_impl n_shifted;
				bigint_impl quotient = 0;

				std::size_t comparison_idx;
				std::size_t operation_idx;

				std::size_t base_idx;
				std::size_t base_trailing_zeros;
				std::size_t total_shft_amount;

				auto perform_subtraction = [&](bigint_impl const& x) {
					unsigned int carry = elements[base_idx] < x.elements[base_idx] ? 1 : 0;
					elements[base_idx] -= x.elements[base_idx];

					for (operation_idx = base_idx + 1; operation_idx <= comparison_idx; ++operation_idx) {
						auto with_carry = x.elements[operation_idx] + carry;
						unsigned int first_carry = (with_carry < carry) ? 1 : 0;

						carry = first_carry | ((elements[operation_idx] < with_carry) ? 1 : 0);
						elements[operation_idx] -= with_carry;
					}
					assert(carry == 0);
					for (; operation_idx <= leading_one_pos.element_pos; ++operation_idx)
						elements[operation_idx] = 0;

					while (elements[comparison_idx] == 0) {
						if (comparison_idx == 0)
							break;
						else
							--comparison_idx;
					}
					leading_one_pos.element_pos = comparison_idx;
					leading_one_pos.bit_pos = log2p1(elements[comparison_idx]);
				};

				if (leading_one_pos.element_pos < n.leading_one_pos.element_pos) {
					return quotient;
				}
				else if (leading_one_pos.element_pos == n.leading_one_pos.element_pos) {
					if (leading_one_pos.bit_pos < n.leading_one_pos.bit_pos) {
						return quotient;
					}
					else if (leading_one_pos.bit_pos == n.leading_one_pos.bit_pos) {
						// Compare *this with n				
						for (comparison_idx = leading_one_pos.element_pos;
							comparison_idx > 0; --comparison_idx)
						{
							// If n is larger, return
							if (n.elements[comparison_idx] > elements[comparison_idx])
								return quotient;

							// If *this is larger, then we can subtract n from *this exactly once
							if (n.elements[comparison_idx] < elements[comparison_idx]) {
								base_idx = 0;
								perform_subtraction(n);

								quotient.leading_one_pos.bit_pos = 1;
								quotient.elements[0] = 1;
								return quotient;
							}
						}
						// If n is larger, return
						if (n.elements[0] > elements[0])
							return quotient;
						// Otherwise, we can subtract n from *this exactly once
						else {
							elements[0] -= n.elements[0];
							std::fill_n(std::begin(elements) + 1, leading_one_pos.element_pos, 0);

							leading_one_pos.element_pos = 0;
							leading_one_pos.bit_pos = log2p1(elements[0]);

							quotient.leading_one_pos.bit_pos = 1;
							quotient.elements[0] = 1;
							return quotient;
						}
					}
					else {
						// Perform bit-wise left-shift
						base_idx = 0;
						base_trailing_zeros = leading_one_pos.bit_pos - n.leading_one_pos.bit_pos;
						total_shft_amount = base_trailing_zeros;

						for (std::size_t idx = leading_one_pos.element_pos; idx > 0; --idx) {
							n_shifted.elements[idx] = (n.elements[idx] << base_trailing_zeros);
							n_shifted.elements[idx] |=
								(n.elements[idx - 1] >> (element_number_of_bits - base_trailing_zeros));
						}
						n_shifted.elements[0] = (n.elements[0] << base_trailing_zeros);
					}
				}
				// leading_one_pos.element_pos > n.leading_one_pos.element_pos
				else {
					// Perform element-wise left-shift and then bit-wise left-shift
					base_idx = leading_one_pos.element_pos - n.leading_one_pos.element_pos;
					if (leading_one_pos.bit_pos > n.leading_one_pos.bit_pos) {
						base_trailing_zeros = leading_one_pos.bit_pos - n.leading_one_pos.bit_pos;
					}
					else if (leading_one_pos.bit_pos < n.leading_one_pos.bit_pos) {
						--base_idx;
						n_shifted.elements[leading_one_pos.element_pos] = 0;
						base_trailing_zeros = element_number_of_bits +
							leading_one_pos.bit_pos - n.leading_one_pos.bit_pos;
					}
					else
						base_trailing_zeros = 0;

					total_shft_amount = base_idx * element_number_of_bits + base_trailing_zeros;
					
					// Element-wise shift
					std::fill_n(std::begin(n_shifted.elements), base_idx, 0);
					std::copy(std::begin(n.elements),
						std::begin(n.elements) + n.leading_one_pos.element_pos + 1,
						std::begin(n_shifted.elements) + base_idx);

					// Bit-wise shift
					if (base_trailing_zeros != 0) {
						for (std::size_t idx = leading_one_pos.element_pos; idx > base_idx; --idx) {
							n_shifted.elements[idx] <<= base_trailing_zeros;
							n_shifted.elements[idx] |=
								(n_shifted.elements[idx - 1] >> (element_number_of_bits - base_trailing_zeros));
						}
						n_shifted.elements[base_idx] <<= base_trailing_zeros;
					}
				}

				// Leading bits of n_shifted and *this are now aligned
				n_shifted.leading_one_pos = leading_one_pos;

				auto compare_and_subtract = [&](auto is_before_iteration) {
					// Compare *this with n_shifted
					for (comparison_idx = leading_one_pos.element_pos;
						comparison_idx != std::size_t(-1); --comparison_idx)
					{
						// If n is larger, right-shift by one bit
						if (n_shifted.elements[comparison_idx] > elements[comparison_idx]) {
							if (base_trailing_zeros == 0) {
								if constexpr (!decltype(is_before_iteration)::value) {
									// If we cannot shift further, stop
									if (base_idx == 0) {
										return false;
									}
								}

								n_shifted.elements[base_idx - 1] =
									(n_shifted.elements[base_idx] << (element_number_of_bits - 1));
								base_trailing_zeros = element_number_of_bits - 1;
								--base_idx;
							}
							else
								--base_trailing_zeros;

							--total_shft_amount;

							if (leading_one_pos.bit_pos == 1) {
								--n_shifted.leading_one_pos.element_pos;
								n_shifted.leading_one_pos.bit_pos = element_number_of_bits;
							}
							else
								--n_shifted.leading_one_pos.bit_pos;

							for (operation_idx = base_idx;
								operation_idx < leading_one_pos.element_pos; ++operation_idx)
							{
								n_shifted.elements[operation_idx] >>= 1;
								n_shifted.elements[operation_idx] |=
									(n_shifted.elements[operation_idx + 1] << (element_number_of_bits - 1));
							}
							n_shifted.elements[leading_one_pos.element_pos] >>= 1;

							comparison_idx = leading_one_pos.element_pos;
							break;
						}

						else if (n_shifted.elements[comparison_idx] < elements[comparison_idx])
							break;
					}

					if constexpr (decltype(is_before_iteration)::value) {
						// Set leading bit position of quotient
						quotient.leading_one_pos.element_pos = base_idx;
						quotient.leading_one_pos.bit_pos = base_trailing_zeros + 1;
					}
					// Update quotient
					quotient.elements[base_idx] |= (element_type(1) << base_trailing_zeros);

					// Exact match
					if (comparison_idx == std::size_t(-1)) {
						// Set *this to zero
						std::fill_n(std::begin(elements), leading_one_pos.element_pos + 1, 0);
						leading_one_pos = { 0, 0 };

						return false;
					}

					// Subtract n_shifted from *this
					perform_subtraction(n_shifted);

					return true;
				};

				if (!compare_and_subtract(std::bool_constant<true>{}))
					return quotient;
				
				do {
					// Right-shift n_shifted to re-align leading ones
					auto element_shft_amount = n_shifted.leading_one_pos.element_pos - leading_one_pos.element_pos;
					std::size_t bit_shft_amount;
					if (leading_one_pos.bit_pos < n_shifted.leading_one_pos.bit_pos) {
						bit_shft_amount = n_shifted.leading_one_pos.bit_pos - leading_one_pos.bit_pos;
					}
					else if (leading_one_pos.bit_pos > n_shifted.leading_one_pos.bit_pos) {
						--element_shft_amount;
						bit_shft_amount = element_number_of_bits +
							n_shifted.leading_one_pos.bit_pos - leading_one_pos.bit_pos;
					}
					else
						bit_shft_amount = 0;

					auto necessary_shft_amount = element_shft_amount * element_number_of_bits + bit_shft_amount;

					if (total_shft_amount < necessary_shft_amount) {
						break;
					}
					total_shft_amount -= necessary_shft_amount;

					base_idx = total_shft_amount / element_number_of_bits;
					base_trailing_zeros = total_shft_amount % element_number_of_bits;

					// Element-wise shift
					if (element_shft_amount != 0) {
						for (std::size_t idx = base_idx; idx <= leading_one_pos.element_pos; ++idx)
							n_shifted.elements[idx] = n_shifted.elements[idx + element_shft_amount];
					}

					// Bit-wise shift
					if (bit_shft_amount != 0) {
						for (std::size_t idx = base_idx; idx < n_shifted.leading_one_pos.element_pos; ++idx) {
							n_shifted.elements[idx] >>= bit_shft_amount;
							n_shifted.elements[idx] |=
								(n_shifted.elements[idx + 1] << (element_number_of_bits - bit_shft_amount));
						}
						n_shifted.elements[n_shifted.leading_one_pos.element_pos] >>= bit_shft_amount;
					}

					// Leading bits of n_shifted and *this are now aligned
					n_shifted.leading_one_pos = leading_one_pos;
				} while(compare_and_subtract(std::bool_constant<false>{}));

				return quotient;
			}
		};


	}
}

#endif