/***************************************************************************
 *   Copyright (C) 2009-2011 by Francesco Biscani                          *
 *   bluescarni@gmail.com                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef PIRANHA_HASH_SET_HPP
#define PIRANHA_HASH_SET_HPP

#include <algorithm>
#include <array>
#include <boost/concept/assert.hpp>
#include <boost/integer_traits.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>
#include <type_traits>

#include "concepts/container_element.hpp"
#include "config.hpp"
#include "debug_access.hpp"
#include "exceptions.hpp"
#include "type_traits.hpp" // For strip_cv_ref.

namespace piranha
{

/// Hash set.
/**
 * 
 * Hash set class with interface similar to \p std::unordered_set.
 * 
 * \section type_requirements Type requirements
 * 
 * \p T must be a model of piranha::concept::ContainerElement. \p Hash and \p Pred must model the
 * concepts in the standard C++ library for the corresponding types of \p std::unordered_set.
 * 
 * \section exception_safety Exception safety guarantee
 * 
 * This class provides the strong exception safety guarantee for all operations apart from the insertion methods,
 * which provide the following guarantee: the table will be either left in the same state as it was before the attempted
 * insertion or it will be emptied.
 * 
 * \section move_semantics Move semantics
 * 
 * Move construction and move assignment will leave the moved-from object equivalent to an empty set whose hasher and
 * equality predicate have been moved-from.
 * 
 * @author Francesco Biscani (bluescarni@gmail.com)
 * 
 * \todo concept assert for hash and pred. Require non-throwing swap, comparison and hashing operators and modify docs accordingly.
 * \todo tests for low-level methods
 */
template <typename T, typename Hash = std::hash<T>, typename Pred = std::equal_to<T>>
class hash_set
{
		BOOST_CONCEPT_ASSERT((concept::ContainerElement<T>));
		// Make friend with debug access class.
		template <typename U>
		friend class debug_access;
		// Node class for bucket element.
		struct node
		{
			typedef typename std::aligned_storage<sizeof(T),alignof(T)>::type storage_type;
			node():m_storage(),m_next(piranha_nullptr) {}
			const T *ptr() const
			{
				return static_cast<const T *>(static_cast<const void *>(&m_storage));
			}
			T *ptr()
			{
				return static_cast<T *>(static_cast<void *>(&m_storage));
			}
			storage_type	m_storage;
			node		*m_next;
		};
		// List constituting the bucket.
		struct list
		{
			template <typename U>
			class iterator_impl: public boost::iterator_facade<iterator_impl<U>,U,boost::forward_traversal_tag>
			{
					typedef typename std::conditional<std::is_const<U>::value,node const *,node *>::type ptr_type;
				public:
					iterator_impl():m_ptr(piranha_nullptr) {}
					explicit iterator_impl(ptr_type ptr):m_ptr(ptr) {}
				private:
					friend class boost::iterator_core_access;
					void increment()
					{
						piranha_assert(m_ptr && m_ptr->m_next);
						m_ptr = m_ptr->m_next;
					}
					bool equal(const iterator_impl &other) const
					{
						return m_ptr == other.m_ptr;
					}
					U &dereference() const
					{
						piranha_assert(m_ptr && m_ptr->m_next);
						return *m_ptr->ptr();
					}
				public:
					ptr_type m_ptr;
			};
			typedef iterator_impl<T> iterator;
			typedef iterator_impl<T const> const_iterator;
			list():m_node() {}
			// TODO: missing move constructor.
			list(list &&) = delete;
			list(const list &other):m_node()
			{
				try {
					auto cur = &m_node;
					auto other_cur = &other.m_node;
					while (other_cur->m_next) {
						if (cur->m_next) {
							piranha_assert(cur->m_next == &terminator);
							// Create a new node with content equal to other_cur
							// and linking forward to the terminator.
							std::unique_ptr<node> new_node(new node());
							::new ((void *)&new_node->m_storage) T(*other_cur->ptr());
							new_node->m_next = &terminator;
							// Link the new node.
							cur->m_next = new_node.release();
							cur = cur->m_next;
						} else {
							// This means this is the first node.
							::new ((void *)&cur->m_storage) T(*other_cur->ptr());
							cur->m_next = &terminator;
						}
						other_cur = other_cur->m_next;
					}
				} catch (...) {
					destroy();
					throw;
				}
			}
			list &operator=(list &&other)
			{
				if (likely(this != &other)) {
					// Destroy the content of this.
					destroy();
					// Do something only if there is content in the other.
					if (other.m_node.m_next) {
						// Move construct current first node with first node of other.
						::new ((void *)&m_node.m_storage) T(std::move(*other.m_node.ptr()));
						// Link remaining content of other into this.
						m_node.m_next = other.m_node.m_next;
						// Destroy first node of other.
						other.m_node.ptr()->~T();
						other.m_node.m_next = piranha_nullptr;
					}
				}
				return *this;
			}
			list &operator=(const list &other)
			{
				if (likely(this != &other)) {
					auto tmp = other;
					*this = std::move(tmp);
				}
				return *this;
			}
			~list()
			{
				destroy();
			}
			template <typename U>
			node *insert(U &&item, typename std::enable_if<std::is_same<T,typename std::decay<U>::type>::value>::type * = piranha_nullptr)
			{
				// NOTE: optimize with likely/unlikely?
				if (m_node.m_next) {
					// Create the new node and forward-link it to the second node.
					std::unique_ptr<node> new_node(new node());
					::new ((void *)&new_node->m_storage) T(std::forward<U>(item));
					new_node->m_next = m_node.m_next;
					// Link first node to the new node.
					m_node.m_next = new_node.release();
					return m_node.m_next;
				} else {
					::new ((void *)&m_node.m_storage) T(std::forward<U>(item));
					m_node.m_next = &terminator;
					return &m_node;
				}
			}
			iterator begin()
			{
				return iterator(&m_node);
			}
			iterator end()
			{
				return iterator((m_node.m_next) ? &terminator : &m_node);
			}
			const_iterator begin() const
			{
				return const_iterator(&m_node);
			}
			const_iterator end() const
			{
				return const_iterator((m_node.m_next) ? &terminator : &m_node);
			}
			bool empty() const
			{
				return !m_node.m_next;
			}
			void destroy()
			{
				node *cur = &m_node;
				while (cur->m_next) {
					// Store the current value temporarily.
					auto old = cur;
					// Assign the next.
					cur = cur->m_next;
					// Destroy the old payload and erase connections
					old->ptr()->~T();
					old->m_next = piranha_nullptr;
					// If the old node was not the initial one, delete it.
					if (old != &m_node) {
						::delete old;
					}
				}
				// After destruction, the list should be equivalent to a default-constructed one.
				piranha_assert(empty());
			}
			static node	terminator;
			node		m_node;
		};
		typedef std::vector<list> container_type;
	public:
		/// Functor type for the calculation of hash values.
		typedef Hash hasher;
		/// Functor type for comparing the items in the set.
		typedef Pred key_equal;
		/// Key type.
		typedef T key_type;
		/// Size type.
		/**
		 * This type is an unsigned integer.
		 */
		typedef typename container_type::size_type size_type;
	private:
		template <typename Key>
		class iterator_impl: public boost::iterator_facade<iterator_impl<Key>,Key,boost::forward_traversal_tag>
		{
				friend class hash_set;
				typedef typename std::conditional<std::is_const<Key>::value,hash_set const,hash_set>::type set_type;
				typedef typename std::conditional<std::is_const<Key>::value,typename list::const_iterator,typename list::iterator>::type it_type;
			public:
				iterator_impl():m_set(piranha_nullptr),m_idx(0u),m_it() {}
				explicit iterator_impl(set_type *set, const size_type &idx, it_type it):
					m_set(set),m_idx(idx),m_it(it) {}
			private:
				friend class boost::iterator_core_access;
				void increment()
				{
					piranha_assert(m_set);
					auto &container = m_set->m_container;
					// Assert that the current iterator is valid.
					piranha_assert(m_idx < container.size());
					piranha_assert(!container[m_idx].empty());
					piranha_assert(m_it != container[m_idx].end());
					++m_it;
					if (m_it == container[m_idx].end()) {
						const size_type container_size = container.size();
						while (true) {
							++m_idx;
							if (m_idx == container_size) {
								m_it = it_type{};
								return;
							} else if (!container[m_idx].empty()) {
								m_it = container[m_idx].begin();
								return;
							}
						}
					}
				}
				bool equal(const iterator_impl &other) const
				{
					piranha_assert(m_set && other.m_set);
					return (m_set == other.m_set && m_idx == other.m_idx && m_it == other.m_it);
				}
				Key &dereference() const
				{
					piranha_assert(m_set && m_idx < m_set->m_container.size() &&
						m_it != m_set->m_container[m_idx].end());
					return *m_it;
				}
			private:
				set_type	*m_set;
				size_type	m_idx;
				it_type		m_it;
		};
	public:
		/// Iterator type.
		typedef iterator_impl<key_type const> iterator;
		/// Const iterator type.
		/**
		 * Equivalent to the iterator type.
		 */
		typedef iterator const_iterator;
		/// Default constructor.
		/**
		 * If not specified, it will default-initialise the hasher and the equality predicate.
		 * 
		 * @param[in] h hasher functor.
		 * @param[in] k equality predicate.
		 * 
		 * @throws unspecified any exception thrown by the copy constructors of <tt>Hash</tt> or <tt>Pred</tt>.
		 */
		hash_set(const hasher &h = hasher(), const key_equal &k = key_equal()):
			m_container(),m_hasher(h),m_key_equal(k),m_n_elements(0u) {}
		/// Constructor from number of buckets.
		/**
		 * Will construct a table whose number of buckets is at least equal to \p n_buckets.
		 * 
		 * @param[in] n_buckets desired number of buckets.
		 * @param[in] h hasher functor.
		 * @param[in] k equality predicate.
		 * 
		 * @throws std::bad_alloc if the desired number of buckets is greater than an implementation-defined maximum.
		 * @throws unspecified any exception thrown by the copy constructors of <tt>Hash</tt> or <tt>Pred</tt>.
		 */
		explicit hash_set(const size_type &n_buckets, const hasher &h = hasher(), const key_equal &k = key_equal()):
			m_container(get_size_from_hint(n_buckets)),m_hasher(h),m_key_equal(k),m_n_elements(0u) {}
		/// Defaulted copy constructor.
		/**
		 * @throws unspecified any exception thrown by the copy constructors of the internal container type, <tt>Hash</tt> or <tt>Pred</tt>.
		 */
		hash_set(const hash_set &) = default;
		/// Move constructor.
		/**
		 * After the move, \p other will have zero buckets and zero elements, and its hasher and equality predicate
		 * will have been used to move-construct their counterparts in \p this.
		 * 
		 * @param[in] other table to be moved.
		 */
		hash_set(hash_set &&other) piranha_noexcept_spec(true) : m_container(std::move(other.m_container)),m_hasher(std::move(other.m_hasher)),
			m_key_equal(std::move(other.m_key_equal)),m_n_elements(std::move(other.m_n_elements))
		{
			// Mark the other as empty, as other's vector will be empty.
			other.m_n_elements = 0u;
		}
		/// Constructor from range.
		/**
		 * Create a table with a copy of a range.
		 * 
		 * @param[in] begin begin of range.
		 * @param[in] end end of range.
		 * @param[in] n_buckets number of initial buckets.
		 * @param[in] h hash functor.
		 * @param[in] k key equality predicate.
		 * 
		 * @throws std::bad_alloc if the desired number of buckets is greater than an implementation-defined maximum.
		 * @throws unspecified any exception thrown by the copy constructors of <tt>Hash</tt> or <tt>Pred</tt>, or arising from
		 * calling insert() on the elements of the range.
		 */
		template <typename InputIterator>
		explicit hash_set(const InputIterator &begin, const InputIterator &end, const size_type &n_buckets = 0u,
			const hasher &h = hasher(), const key_equal &k = key_equal()):
			m_container(get_size_from_hint(n_buckets)),m_hasher(h),m_key_equal(k),m_n_elements(0u)
		{
			for (auto it = begin; it != end; ++it) {
				insert(*it);
			}
		}
		/// Constructor from initializer list.
		/**
		 * Will insert() all the elements of the initializer list, ignoring the return value of the operation.
		 * Hash functor and equality predicate will be default-constructed.
		 * 
		 * @param[in] list initializer list of elements to be inserted.
		 * 
		 * @throws std::bad_alloc if the desired number of buckets is greater than an implementation-defined maximum.
		 * @throws unspecified any exception thrown by either insert() or of the default constructor of <tt>Hash</tt> or <tt>Pred</tt>.
		 */
		template <typename U>
		hash_set(std::initializer_list<U> list):m_container(get_size_from_hint(list.size())),m_hasher(),m_key_equal(),m_n_elements(0u)
		{
			for (auto it = list.begin(); it != list.end(); ++it) {
				insert(*it);
			}
		}
		/// Destructor.
		/**
		 * No side effects.
		 */
		~hash_set() piranha_noexcept_spec(true)
		{
			piranha_assert(sanity_check());
		}
		/// Copy assignment operator.
		/**
		 * @param[in] other assignment argument.
		 * 
		 * @return reference to \p this.
		 * 
		 * @throws unspecified any exception thrown by the copy constructors of the internal container type, <tt>Hash</tt> or <tt>Pred</tt>.
		 */
		hash_set &operator=(const hash_set &other)
		{
			if (likely(this != &other)) {
				hash_set tmp(other);
				*this = std::move(tmp);
			}
			return *this;
		}
		/// Move assignment operator.
		/**
		 * @param[in] other table to be moved into \p this.
		 * 
		 * @return reference to \p this.
		 */
		hash_set &operator=(hash_set &&other) piranha_noexcept_spec(true)
		{
			if (likely(this != &other)) {
				m_hasher = std::move(other.m_hasher);
				m_key_equal = std::move(other.m_key_equal);
				m_n_elements = std::move(other.m_n_elements);
				m_container = std::move(other.m_container);
				// Zero out other.
				other.m_n_elements = 0u;
			}
			return *this;
		}
		/// Const begin iterator.
		/**
		 * @return hash_set::const_iterator to the first element of the table, or end() if the table is empty.
		 */
		const_iterator begin() const
		{
			// NOTE: this could take a while in case of an empty table with lots of buckets. Take a shortcut
			// taking into account the number of elements in the table - if zero, go directly to end()?
			const_iterator retval;
			retval.m_set = this;
			size_type idx = 0u;
			for (; idx < m_container.size(); ++idx) {
				if (!m_container[idx].empty()) {
					break;
				}
			}
			retval.m_idx = idx;
			// If we are not at the end, assign proper iterator.
			if (idx != m_container.size()) {
				retval.m_it = m_container[idx].begin();
			}
			return retval;
		}
		/// Const end iterator.
		/**
		 * @return hash_set::const_iterator to the position past the last element of the table.
		 */
		const_iterator end() const
		{
			const_iterator retval;
			retval.m_set = this;
			retval.m_idx = m_container.size();
			return retval;
		}
		/// Begin iterator.
		/**
		 * @return hash_set::iterator to the first element of the table, or end() if the table is empty.
		 */
		iterator begin()
		{
			return static_cast<hash_set const *>(this)->begin();
		}
		/// End iterator.
		/**
		 * @return hash_set::iterator to the position past the last element of the table.
		 */
		iterator end()
		{
			return static_cast<hash_set const *>(this)->end();
		}
		/// Number of elements contained in the table.
		/**
		 * @return number of elements in the table.
		 */
		size_type size() const
		{
			return m_n_elements;
		}
		/// Test for empty table.
		/**
		 * @return \p true if size() returns 0, \p false otherwise.
		 */
		bool empty() const
		{
			return !size();
		}
		/// Number of buckets.
		/**
		 * @return number of buckets in the table.
		 */
		size_type n_buckets() const
		{
			return m_container.size();
		}
		/// Load factor.
		/**
		 * @return <tt>size() / n_buckets()</tt>, or 0 if the table is empty.
		 */
		double load_factor() const
		{

			return (m_container.size()) ? static_cast<double>(m_n_elements) / m_container.size() : 0.;
		}
		/// Index of destination bucket.
		/**
		 * Index to which \p k would belong, were it to be inserted into the table.
		 * 
		 * @param[in] k input argument.
		 * 
		 * @return index of the destination bucket for \p k.
		 * 
		 * @throws piranha::zero_division_error if n_buckets() returns zero.
		 * @throws unspecified any exception thrown by _bucket().
		 */
		size_type bucket(const key_type &k) const
		{
			if (unlikely(!m_container.size())) {
				piranha_throw(zero_division_error,"cannot calculate bucket index in an empty table");
			}
			return _bucket(k);
		}
		/// Find element.
		/**
		 * @param[in] k element to be located.
		 * 
		 * @return hash_set::const_iterator to <tt>k</tt>'s position in the table, or end() if \p k is not in the table.
		 * 
		 * @throws unspecified any exception thrown by _find().
		 */
		const_iterator find(const key_type &k) const
		{
			if (unlikely(!m_container.size())) {
				return end();
			}
			return _find(k,_bucket(k));
		}
		/// Find element.
		/**
		 * @param[in] k element to be located.
		 * 
		 * @return hash_set::iterator to <tt>k</tt>'s position in the table, or end() if \p k is not in the table.
		 * 
		 * @throws unspecified any exception thrown by _find().
		 */
		iterator find(const key_type &k)
		{
			return static_cast<const hash_set *>(this)->find(k);
		}
		/// Maximum load factor.
		/**
		 * @return the maximum load factor allowed before a resize.
		 */
		double get_max_load_factor() const
		{
			// Maximum load factor hard-coded to 0.75.
			return 0.75;
		}
		/// Insert element.
		/**
		 * This template is activated only if \p T and \p U are the same type, aside from cv qualifications and references.
		 * If no other key equivalent to \p k exists in the table, the insertion is successful and returns the <tt>(it,true)</tt>
		 * pair - where \p it is the position in the table into which the object has been inserted. Otherwise, the return value
		 * will be <tt>(it,false)</tt> - where \p it is the position of the existing equivalent object.
		 * 
		 * @param[in] k object that will be inserted into the table.
		 * 
		 * @return <tt>(hash_set::iterator,bool)</tt> pair containing an iterator to the newly-inserted object (or its existing
		 * equivalent) and the result of the operation.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - hash_set::key_type's copy constructor,
		 * - _find().
		 * @throws std::bad_alloc if the operation results in a resize of the table past an implementation-defined
		 * maximum number of buckets.
		 */
		template <typename U>
		std::pair<iterator,bool> insert(U &&k, typename std::enable_if<std::is_same<T,typename strip_cv_ref<U>::type>::value>::type * = piranha_nullptr)
		{
			if (unlikely(!m_container.size() || (static_cast<double>(m_n_elements) + 1.) / m_container.size() > get_max_load_factor())) {
				increase_size();
			}
			const auto bucket_idx = _bucket(k), it = _find(k,bucket_idx);
			if (it != end()) {
				return std::make_pair(it,false);
			}
			const auto it_retval = _unique_insert(std::forward<U>(k),bucket_idx);
			++m_n_elements;
			return std::make_pair(it_retval,true);
		}
		/// Erase element.
		/**
		 * Erase the element to which \p it points. \p it must be a valid iterator
		 * pointing to an element of the table.
		 * 
		 * Erasing an element invalidates all iterators pointing to elements in the same bucket
		 * as the erased element.
		 * 
		 * @param[in] it iterator to the element of the table to be removed.
		 */
		void erase(const iterator &it)
		{
			// Verify the iterator is valid.
			piranha_assert(!empty() && it.m_set == this && it.m_idx < m_container.size() &&
				!m_container[it.m_idx].empty() && it.m_it.m_ptr != m_container[it.m_idx].end().m_ptr);
			auto &bucket = m_container[it.m_idx];
			// If the pointed-to element is the first one in the bucket, we need special care.
			if (&*it == &*bucket.m_node.ptr()) {
				// Destroy the payload.
				bucket.m_node.ptr()->~T();
				if (bucket.m_node.m_next == &bucket.terminator) {
					// Special handling if this was the only element.
					bucket.m_node.m_next = piranha_nullptr;
				} else {
					// Store the link in the second element.
					auto tmp = bucket.m_node.m_next->m_next;
					// Move-construct from the second element, and then destroy it.
					::new ((void *)&bucket.m_node.m_storage) T(std::move(*bucket.m_node.m_next->ptr()));
					bucket.m_node.m_next->ptr()->~T();
					::delete bucket.m_node.m_next;
					// Establish the new link.
					bucket.m_node.m_next = tmp;
				}
			} else {
				auto b_it = bucket.begin();
				auto prev_b_it = b_it;
				const auto b_it_f = bucket.end();
				++b_it;
				for (; b_it != b_it_f; ++b_it, ++prev_b_it) {
					if (&*b_it == &*it) {
						// Assign to the previous element the next link of the current one.
						prev_b_it.m_ptr->m_next = b_it.m_ptr->m_next;
						// Delete the current one.
						b_it.m_ptr->ptr()->~T();
						::delete b_it.m_ptr;
						break;
					};
				}
				// We never want to go throught the whole list, it means the element
				// to which 'it' refers is not here.
				piranha_assert(b_it != b_it_f);
			}
			piranha_assert(m_n_elements);
			--m_n_elements;
		}
		/// Remove all elements.
		/**
		 * After this call, size() and n_buckets() will both return zero.
		 */
		void clear()
		{
			m_container = container_type();
			m_n_elements = 0u;
		}
		/// Swap content.
		/**
		 * Will use \p std::swap to swap hasher and equality predicate.
		 * 
		 * @param[in] other swap argument.
		 * 
		 * @throws unspecified any exception thrown by swapping hasher or equality predicate via \p std::swap.
		 */
		void swap(hash_set &other)
		{
			m_container.swap(other.m_container);
			std::swap(m_hasher,other.m_hasher);
			std::swap(m_key_equal,other.m_key_equal);
			std::swap<size_type>(m_n_elements,other.m_n_elements);
		}
		/** @name Low-level interface
		 * Low-level methods and types.
		 */
		//@{
		/// Mutable iterator.
		/**
		 * This iterator type provides non-const access to the elements of the table. Please note that modifications
		 * to an existing element of the table might invalidate the relation between the element and its position in the table.
		 * After such modifications of one or more elements, the only valid operation is hash_set::clear() (destruction of the
		 * table before calling hash_set::clear() will lead to assertion failures in debug mode).
		 */
		typedef iterator_impl<key_type> _m_iterator;
		/// Mutable begin iterator.
		/**
		 * @return hash_set::_m_iterator to the beginning of the table.
		 */
		_m_iterator _m_begin()
		{
			// NOTE: this could take a while in case of an empty table with lots of buckets. Take a shortcut
			// taking into account the number of elements in the table - if zero, go directly to end()?
			_m_iterator retval;
			retval.m_set = this;
			size_type idx = 0u;
			for (; idx < m_container.size(); ++idx) {
				if (!m_container[idx].empty()) {
					break;
				}
			}
			retval.m_idx = idx;
			// If we are not at the end, assign proper iterator.
			if (idx != m_container.size()) {
				retval.m_it = m_container[idx].begin();
			}
			return retval;
		}
		/// Mutable end iterator.
		/**
		 * @return hash_set::_m_iterator to the end of the table.
		 */
		_m_iterator _m_end()
		{
			_m_iterator retval;
			retval.m_set = this;
			retval.m_idx = m_container.size();
			return retval;
		}
		/// Insert unique element (low-level).
		/**
		 * This template is activated only if \p T and \p U are the same type, aside from cv qualifications and references.
		 * The parameter \p bucket_idx is the index of the destination bucket for \p k and, for a
		 * table with a nonzero number of buckets, must be equal to the output
		 * of bucket() before the insertion.
		 * 
		 * This method will not check if a key equivalent to \p k already exists in the table, it will not
		 * update the number of elements present in the table after the insertion, nor it will check
		 * if the value of \p bucket_idx is correct.
		 * 
		 * @param[in] k object that will be inserted into the table.
		 * @param[in] bucket_idx destination bucket for \p k.
		 * 
		 * @return iterator pointing to the newly-inserted element.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - hash_set::key_type's copy constructor,
		 * - <tt>std::vector::push_back()</tt>.
		 */
		template <typename U>
		iterator _unique_insert(U &&k, const size_type &bucket_idx,
			typename std::enable_if<std::is_same<T,typename strip_cv_ref<U>::type>::value>::type * = piranha_nullptr)
		{
			// Assert that key is not present already in the table.
			piranha_assert(find(std::forward<U>(k)) == end());
			// Assert bucket index is correct.
			piranha_assert(bucket_idx == _bucket(k));
			auto ptr = m_container[bucket_idx].insert(std::forward<U>(k));
			return iterator(this,bucket_idx,typename list::const_iterator(ptr));
		}
		/// Find element (low-level).
		/**
		 * Locate element in the table. The parameter \p bucket_idx is the index of the destination bucket for \p k and, for 
		 * a table with a nonzero number of buckets, must be equal to the output
		 * of bucket() before the insertion. This method will not check if the value of \p bucket_idx is correct.
		 * 
		 * @param[in] k element to be located.
		 * @param[in] bucket_idx index of the destination bucket for \p k.
		 * 
		 * @return hash_set::iterator to <tt>k</tt>'s position in the table, or end() if \p k is not in the table.
		 * 
		 * @throws unspecified any exception thrown by:
		 * - _bucket(),
		 * - the comparison functor.
		 */
		const_iterator _find(const key_type &k, const size_type &bucket_idx) const
		{
			// Assert bucket index is correct.
			piranha_assert(bucket_idx == _bucket(k));
			const auto &b = m_container[bucket_idx];
			const auto it_f = b.end();
			for (auto it = b.begin(); it != it_f; ++it) {
				if (m_key_equal(*it,k)) {
					return iterator(this,bucket_idx,it);
				}
			}
			return end();
		}
		/// Index of destination bucket (low-level).
		/**
		 * Equivalent to bucket(), with the exception that this method will not check
		 * if the number of buckets is zero. In such a case, an integral division by zero will occur.
		 * 
		 * @param[in] k input argument.
		 * 
		 * @return index of the destination bucket for \p k.
		 * 
		 * @throws unspecified any exception thrown by the hashing functor.
		 */
		size_type _bucket(const key_type &k) const
		{
			piranha_assert(m_container.size());
			return m_hasher(k) % m_container.size();
		}
		//@}
	private:
		// Run a consistency check on the table, will return false if something is wrong.
		bool sanity_check() const
		{
			size_type count = 0u;
			for (size_type i = 0u; i < m_container.size(); ++i) {
				for (auto it = m_container[i].begin(); it != m_container[i].end(); ++it) {
					if (_bucket(*it) != i) {
						return false;
					}
					++count;
				}
			}
			if (count != m_n_elements) {
				return false;
			}
			if (m_container.size() != table_sizes[get_size_index()]) {
				return false;
			}
			// Check size is consistent with number of iterator traversals.
			count = 0u;
			for (auto it = begin(); it != end(); ++it, ++count) {}
			if (count != m_n_elements) {
				return false;
			}
			return true;
		}
		static const std::size_t n_available_sizes =
#if defined(PIRANHA_64BIT_MODE)
			41u;
#else
			33u;
#endif
		typedef std::array<size_type,n_available_sizes> table_sizes_type;
		static const table_sizes_type table_sizes;
		// Increase table size at least to the next available size.
		void increase_size()
		{
			auto cur_size_index = get_size_index();
			if (unlikely(cur_size_index == n_available_sizes - static_cast<decltype(cur_size_index)>(1))) {
				throw std::bad_alloc();
			}
			++cur_size_index;
			// Create new table with needed amount of buckets.
			hash_set new_table(table_sizes[cur_size_index],m_hasher,m_key_equal);
			try {
				const auto it_f = _m_end();
				for (auto it = _m_begin(); it != it_f; ++it) {
					const auto new_idx = new_table._bucket(*it);
					new_table._unique_insert(std::move(*it),new_idx);
				}
			} catch (...) {
				// Clear up both this and the new table upon any kind of error.
				clear();
				new_table.clear();
				throw;
			}
			// Retain the number of elements.
			new_table.m_n_elements = m_n_elements;
			// Clear the old table.
			clear();
			// Assign the new table.
			*this = std::move(new_table);
		}
		// Return the index in the table_sizes array of the current table size.
		std::size_t get_size_index() const
		{
			auto range = std::equal_range(table_sizes.begin(),table_sizes.end(),m_container.size());
			// Paranoia check.
			typedef typename std::iterator_traits<typename table_sizes_type::iterator>::difference_type difference_type;
			static_assert(
				static_cast<typename std::make_unsigned<difference_type>::type>(boost::integer_traits<difference_type>::const_max) >=
				n_available_sizes,"Overflow error."
			);
			piranha_assert(range.second - range.first == 1);
			return std::distance(table_sizes.begin(),range.first);
		}
		// Get table size at least equal to hint.
		static size_type get_size_from_hint(const size_type &hint)
		{
			auto it = std::lower_bound(table_sizes.begin(),table_sizes.end(),hint);
			if (it == table_sizes.end()) {
				throw std::bad_alloc();
			}
			piranha_assert(*it >= hint);
			return *it;
		}
	private:
		container_type	m_container;
		hasher		m_hasher;
		key_equal	m_key_equal;
		size_type	m_n_elements;
};

template <typename T, typename Hash, typename Pred>
typename hash_set<T,Hash,Pred>::node hash_set<T,Hash,Pred>::list::terminator;

template <typename T, typename Hash, typename Pred>
const typename hash_set<T,Hash,Pred>::table_sizes_type hash_set<T,Hash,Pred>::table_sizes = { {
	0ull,
	1ull,
	3ull,
	5ull,
	11ull,
	23ull,
	53ull,
	97ull,
	193ull,
	389ull,
	769ull,
	1543ull,
	3079ull,
	6151ull,
	12289ull,
	24593ull,
	49157ull,
	98317ull,
	196613ull,
	393241ull,
	786433ull,
	1572869ull,
	3145739ull,
	6291469ull,
	12582917ull,
	25165843ull,
	50331653ull,
	100663319ull,
	201326611ull,
	402653189ull,
	805306457ull,
	1610612741ull,
	3221225473ull
#if defined(PIRANHA_64BIT_MODE)
	,
	6442450939ull,
	12884901893ull,
	25769803799ull,
	51539607551ull,
	103079215111ull,
	206158430209ull,
	412316860441ull,
	824633720831ull
#endif
} };

}

#endif
