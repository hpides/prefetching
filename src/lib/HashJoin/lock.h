/*
    Copyright 2011, Spyros Blanas.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef LOCK_H
#define LOCK_H

typedef volatile char Lock_t;

inline void unlock(Lock_t * _l) __attribute__((always_inline));
inline void lock(Lock_t * _l) __attribute__((always_inline));
inline int  tas(volatile char * lock) __attribute__((always_inline));

/*
 * Non-recursive spinlock. Using `xchg` and `ldstub` as in PostgresSQL.
 */
/* Call blocks and retunrs only when it has the lock. */
inline void lock(Lock_t * _l) {
    while(tas(_l)) {
#if defined(__i386__) || defined(__x86_64__)
        __asm__ __volatile__ ("pause\n");
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield\n");
#endif
    }
}

/** Unlocks the lock object. */
inline void unlock(Lock_t *_l)
{
#if defined(__arch64__) || defined(__arm__)
    __sync_lock_release(_l);
    printf("lock released");
#else
    *_l = 0;
#endif
}

inline int tas(volatile char * lock)
{
    register char res = 1;
#if defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__ (
                          "lock xchgb %0, %1\n"
                          : "+q"(res), "+m"(*lock)
                          :
                          : "memory", "cc");
#elif defined(__sparc__)
    __asm__ __volatile__ (
                          "ldstub [%2], %0"
                          : "=r"(res), "+m"(*lock)
                          : "r"(lock)
                          : "memory");
#elif defined(__aarch64__) || defined(__arm__)
    return __sync_lock_test_and_set(lock, 1);
#else
    printf("ERROR: TAS NOT SUPPORTED");
// #error TAS not defined for this architecture.
#endif
    return res;
}

#endif /* LOCK_H */
