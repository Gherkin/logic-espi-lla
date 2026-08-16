#ifndef ESPI_FRAME_V2_SINK_RECORDING_H
#define ESPI_FRAME_V2_SINK_RECORDING_H

#include "FrameV2Sink.h"

#include <vector>

namespace espi_saleae
{

// Test-side view of the recording FrameV2 sink. Only FrameV2SinkRecording.cpp
// defines these, and that file is linked into test binaries only -- so
// including this header from the plugin would fail to link, which is the
// intended direction of the seam.

const std::vector<TransactionSummary>& RecordedTransactionsV2();
const std::vector<FieldRecord>& RecordedFieldsV2();
void ClearRecordedTransactionsV2();

} // namespace espi_saleae

#endif // ESPI_FRAME_V2_SINK_RECORDING_H
