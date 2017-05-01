#pragma once

namespace vmodule {
/**
 * Any class that inherits from NonCopyable will ... not be copyable (Duh!)
 */
class NonCopyable {
private:
	inline NonCopyable(const NonCopyable&) {}
	inline NonCopyable& operator=(const NonCopyable&)
	{
		return *this;
	}
public:
	inline NonCopyable() {}
};

/**
 * This will create a new predicate from an old predicate P with
 *  inverse truth value. This predicate is safe to use in a
 *  TightConditionVariable<P>
 */
template<class P>
class InversePredicate {
private:
	P predicate;
public:
	inline InversePredicate(P predicate_) :
			predicate(predicate_) {}
	inline InversePredicate(const InversePredicate<P>& other) :
			predicate(other.predicate) {}

	inline InversePredicate<P>& operator=(InversePredicate<P>& other)
	{
		predicate = other.predicate;
	}

	inline bool operator!() const {
		return !(!predicate);
	}
};

};

