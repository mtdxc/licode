#ifndef ERIZO_SRC_ERIZO_RTP_SEQUENCENUMBERTRANSLATOR_H_
#define ERIZO_SRC_ERIZO_RTP_SEQUENCENUMBERTRANSLATOR_H_

#include <utility>
#include <vector>

#include "./logger.h"
#include "pipeline/Service.h"

namespace erizo {

enum SequenceNumberType {
  Valid = 0,
  Skip = 1,
  Discard = 2,
  Generated = 3
};

struct SequenceNumber {
  SequenceNumber(){}
  SequenceNumber(uint16_t input_, uint16_t output_, SequenceNumberType type_) :
      input{input_}, output{output_}, type{type_} {}
  // index in in_XXX_buffer_
  uint16_t input = 0;
  uint16_t output = 0;
  SequenceNumberType type = Valid;
};

class SequenceNumberTranslator: public Service, public std::enable_shared_from_this<SequenceNumberTranslator> {
  DECLARE_LOGGER();

 public:
  SequenceNumberTranslator();

  SequenceNumber get(uint16_t input_sequence_number) const;
  SequenceNumber get(uint16_t input_sequence_number, bool skip);
  SequenceNumber reverse(uint16_t output_sequence_number) const;
  void reset();
  SequenceNumber generate();

 private:
  void add(SequenceNumber sequence_number);
  uint16_t fill(uint16_t first, uint16_t last);
  SequenceNumber& internalGet(uint16_t input_sequence_number);
  SequenceNumber& internalReverse(uint16_t output_sequence_number);
  void updateLastOutputSequenceNumber(bool skip, uint16_t output_sequence_number);

 private:
  std::vector<SequenceNumber> in_out_buffer_;
  std::vector<SequenceNumber> out_in_buffer_;
  uint16_t first_input_sequence_number_ = 0;
  uint16_t last_input_sequence_number_ = 0;
  uint16_t last_output_sequence_number_ = 0;
  uint16_t offset_ = 0;
  bool initialized_ = false;
  bool reset_ = false;
};
}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_SEQUENCENUMBERTRANSLATOR_H_
