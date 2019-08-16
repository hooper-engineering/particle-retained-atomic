/** @file ParticleRetainedAtomic.h
 *  @brief A helper library to atomically store data in persistent RAM
 *
 *  @author    Daniel Hooper
 *  @copyright Copyright Hooper Engineering, LLC 2019
 *  @date      March 11, 2019
 *
 *  @license   MIT
 */

/*
Copyright 2019 Hooper Engineering, LLC

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <Particle.h>

Logger retlog("ret-atomic");

/**
 * A persistent data structure used by the ParticleRetainedAtomic library
 *
 * An object of this type should be declared 'retained' in the global scope of
 * the program, and passed into the ParticleRetainedAtomic object's constructor.
 *
 * @warning Each instance of ParticleRetainedAtomic needs to have a separate
 * ParticleRetainedAtomicData_t instance. *Do not re-use it!*
 */
typedef struct {
  uint16_t seqNumA;             // sequence numbers help resolve edge cases
  uint16_t seqNumB;             // of A.isValid() && B.isValid()
  uint32_t checksumA;
  uint32_t checksumB;
} ParticleRetainedAtomicData_t;


/**
 * A helper class template to (relatively) safely and atomically store data across device resets.
 *
 * This class provides a transactional save model to collect changes that are made
 * to the given state storage object of type &lt;T&gt;. Changes can be made freely
 * to that object via
 *
 * `ParticleRetainedAtomicInstance->myMember = 12345;`
 *
 * A call to `ParticleRetainedAtomicInstance.save()` will then freeze this
 * change and store it to RAM. Changes made after the `save()` but before another
 * `save()` call will be lost after a reboot.
 *
 * See README.md for detailed examples.
 */
template<typename T>
class ParticleRetainedAtomic {

private:

  template<typename U>
  class SavePage {

  private:
    T& m_data;
    uint16_t& m_seqNum;
    uint32_t& m_checksum;
    uint32_t calculateChecksum();

  public:
    friend class ParticleRetainedAtomic;

    SavePage(U& data, uint16_t& seqnum, uint32_t& checksum);

    void init(const U& initData);            // initializes the SavePage data area
    void clearChecksum(void);                // overrwrites checksum
    bool isValid(void);                      // checks checksum
    void writeChecksum(void);                // writes new checksum
    SavePage<U>& operator=(const SavePage<U>& rhs);
  };

  SavePage<T> m_a, m_b;

  SavePage<T>* m_scratchpad;  // points to m_dataA or m_dataB
  SavePage<T>* m_saved;       // points to m_dataB or m_dataA

public:

  ParticleRetainedAtomic(T& retainedPageA, T& retainedPageB, ParticleRetainedAtomicData_t& retainedData, const T& defaultValue);
  T& getScratchpad();     // returns a reference to the scratchpad object/data
  T* operator->(void);    // thisobject->youraccessor
  void save(void);

};


/**
 * Construct a SavePage object that points to the given data and checksum
 * @param data     A retained data type
 * @param seqnum   A retained uint16_t that holds the sequence number
 * @param checksum A retained uint32_t that holds the data checksum
 */
template <typename T> template <typename U> inline
ParticleRetainedAtomic<T>::SavePage<U>::SavePage(U& data, uint16_t& seqnum, uint32_t& checksum) :
m_data(data), m_seqNum(seqnum), m_checksum(checksum) {
  retlog.trace("SavePage constructor");
}

/**
 * Initialize the SavePage with given data
 * @param initData Reference to data default value
 */
template <typename T> template <typename U> inline
void ParticleRetainedAtomic<T>::SavePage<U>::init(const U& initData) {
  m_data = initData;
  m_seqNum = 1;
  retlog.trace("SavePage init");
}

/**
 * Invalidates a data page
 *
 * Modifies the data page checksum to make it invlaid.
 */
template <typename T> template <typename U> inline
void ParticleRetainedAtomic<T>::SavePage<U>::clearChecksum() {
  m_checksum = ~(m_checksum);
  retlog.trace("SavePage clearChecksum");
}

/**
 * Checks the checksum against the data in object
 * @return true if valid checksum is found
 */
template <typename T> template <typename U> inline
bool ParticleRetainedAtomic<T>::SavePage<U>::isValid() {
  retlog.trace("SavePage isValid (stored:%lu calc:%lu)", m_checksum, calculateChecksum());
  return (calculateChecksum() == m_checksum);
}

/**
 * Saves a current checksum
 */
template <typename T> template <typename U> inline
void ParticleRetainedAtomic<T>::SavePage<U>::writeChecksum() {
  m_checksum = calculateChecksum();
  retlog.trace("SavePage writeChecksum %lu", m_checksum);
}

/**
 * Copies the data referenced in the SavePage object to another SavePage object
 *
 * Since this class only stores references to 'retained' memory that was declared
 * in the global scope, this makes it possible to write that referenced data from
 * one SavePage to another with a simpler notation.
 *
 * @note This automatically increments the seqNum
 *
 * @param rhs   Right operand
 */
template <typename T> template <typename U> inline
ParticleRetainedAtomic<T>::SavePage<U>& ParticleRetainedAtomic<T>::SavePage<U>::operator=(const SavePage<U>& rhs) {

retlog.trace("SavePage operator=");
  if (this == &rhs) return *this;

  m_data      = rhs.m_data;

  // zero seqNum is invalid
  if (rhs.m_seqNum == UINT16_MAX) m_seqNum = 1;
  else                            m_seqNum = rhs.m_seqNum+1;

  m_checksum  = rhs.m_checksum;

  return *this;
}


/**
 * Calculates a sum of all bytes in the saved data in this object
 * @return Sum of bytes of data. If greater than uint32, overflows to 0 and starts over.
 *
 * @note This checksum function is extremely rudimentary and is a good candidate
 * for more work.
 */
template <typename T> template <typename U> inline
uint32_t ParticleRetainedAtomic<T>::SavePage<U>::calculateChecksum() {

  uint32_t sum = 0;

  uint8_t* p = (uint8_t*)&m_data;
  const uint8_t* pEnd = (uint8_t*)&m_data + sizeof(T);

  //retlog.trace("sizeof(T)=%u", sizeof(T));

  do {
    //retlog.trace("sum: %x pval: %x paddr: %p ", sum, *p, p);
    sum += *(p++);
  } while (p < pEnd);

  // include sequence number in checksum calculation
  sum += (0xff00 & m_seqNum) >> 8;
  sum += m_seqNum & 0xff;

  retlog.trace("SavePage calculateChecksum sum: %lx checksum: %lx", sum, ~sum);

  return ~sum;
}

/**
 * Create a ParticleRetainedAtomic object to be stored in provided retained RAM pointers
 * @param retainedPageA Reference to retained type T (Page A)
 * @param retainedPageB Reference to retained type T (Page B)
 * @param retainedData  Reference to a retained ParticleRetainedAtomicData_t structure
 * @param defaultValue  Reference to a type T initialized with default values
 *
 * This constructor
 * 1. Retains a reference to the provided retained data structures/objects for use
 * 2. Checks checksums and copies the valid page (Page A or Page B) to the other page
 * 3. If both are valid, use the page with the most recent sequence number
 * 4. If neither are valid, copy the defaultValue and save
 */
template<typename T> inline
ParticleRetainedAtomic<T>::ParticleRetainedAtomic(
                T& retainedPageA,
                T& retainedPageB,
                ParticleRetainedAtomicData_t& retainedData,
                const T& defaultValue) :
                m_a(SavePage<T>(retainedPageA, retainedData.seqNumA, retainedData.checksumA)),
                m_b(SavePage<T>(retainedPageB, retainedData.seqNumB, retainedData.checksumB)) {

retlog.trace("ParticleRetainedAtomic constructor");

  // Remember that when save() is called, m_scratchpad gets checksummed and frozen.
  if (m_a.isValid()) {
    if (m_a.m_seqNum == 0)  retlog.error("A is valid but seqence number is zero!!!");

    if (m_b.isValid()) {
      if (m_b.m_seqNum == 0) retlog.error("B is valid but sequence number is zero!!!");

      retlog.trace("Both stored pages are valid! Using sequence number to resolve. A:%u B:%u", m_a.m_seqNum, m_b.m_seqNum);

      if (m_a.m_seqNum > m_b.m_seqNum || (m_b.m_seqNum == UINT16_MAX && m_a.m_seqNum == 1)) {
        m_scratchpad = &m_a;
        m_saved = &m_b;
      }
      else if (m_b.m_seqNum > m_a.m_seqNum || (m_a.m_seqNum == UINT16_MAX && m_b.m_seqNum == 1)) {
        m_scratchpad = &m_b;
        m_saved = &m_a;
      }
      else {
        retlog.error("Something went wrong validating the sequence numbers. Restored default values.");
        m_a.init(defaultValue);
        m_scratchpad = &m_a;
        m_saved = &m_b;
      }
    }
    else {  // !m_b.isValid(), so use page A
      m_scratchpad = &m_a;
      m_saved = &m_b;
    }
  }
  else if (m_b.isValid()) {
    m_scratchpad = &m_b;
    m_saved = &m_a;
  }
  else {  // no valid page, copy default value to page A then save it.
    retlog.trace("No valid pages, values set from default!");
    m_a.init(defaultValue);
    m_scratchpad = &m_a;
    m_saved = &m_b;
  }
  save();
}


/**
 * Returns a reference to the scratchpad data object
 *
 * This object should be used to save data to the scratchpad object.
 *
 * @return A reference to scratchpad data object of template type &lt;T&gt;
 */
template<typename T> inline
T& ParticleRetainedAtomic<T>::getScratchpad() {
  retlog.trace("ParticleRetainedAtomic getScratchpad");
  return m_scratchpad->m_data;
}

/**
 * An alias for getScratchpad()
 *
 * This operator alias should allow the usage ParticleRetainedAtomicObject->templateTypeAccessor
 * where templateTypeAccessor is a member of the templated class type.
 *
 * @note operator-> must return a void* type which allows it to be used by the
 * caller in the expected way, although GCC seems to be aware of the type and
 * can do static, compile time member checks on the T type object.
 */
template<typename T> inline
T* ParticleRetainedAtomic<T>::operator->() {
  retlog.trace("ParticleRetainedAtomic operator->");
  return &(this->getScratchpad());
}

/**
 * Atomically saves the scratchpad data.
 *
 * This function saves the checksum of the current scratchpad page, copies
 * the former scratchpad contents to the other page, and invalidates its checksum.
 *
 * The pointers to save and scratch are now swapped and all modifications occur
 * in the new scratchpad area.
 */
template<typename T> inline
void ParticleRetainedAtomic<T>::save(void) {
  retlog.trace("ParticleRetainedAtomic save");
  m_scratchpad->writeChecksum();        // write valid checksum to scratchpad-- this data is now safely stored
  *m_saved = *m_scratchpad;             // copy most current data from scrtatchpad to (previously) saved page
  m_saved->clearChecksum();             // invalidate (previously) saved page

  SavePage<T>* a = m_saved;             // now swap pointers so that saved becomes scratch and vice versa
  m_saved = m_scratchpad;
  m_scratchpad = a;
}
