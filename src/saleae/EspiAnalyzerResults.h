#ifndef ESPI_ANALYZER_RESULTS_H
#define ESPI_ANALYZER_RESULTS_H

#include <AnalyzerResults.h>

#include <string>
#include <vector>

namespace espi_saleae
{

// ---------------------------------------------------------------------------
//  Results.
//
//  HOW A FIELD'S TEXT GETS HERE, AND WHY IT IS STORED RATHER THAN REBUILT.
//
//  A Frame holds two U64s, a type byte and a flags byte -- see docs/PLAN.md
//  section 6, gotcha 8. The core's decode is a tree of espi::Field, each with a
//  name and a formatted string, and none of that fits in a Frame. So the text
//  has to be either carried alongside, or regenerated at bubble time from an
//  identifier packed into the Frame.
//
//  Regenerating needs a stable field-kind enum covering every field the decoder
//  emits -- and that enum is the FrameV2 type-name contract that docs/PLAN.md
//  section 10 schedules for phase 8, naming it the thing downstream HLAs bind
//  to and the expensive thing to rename. Phase 4 has no FrameV2 payload to
//  design those names against, so inventing them here is inventing them at the
//  worst possible moment.
//
//  Hence a parallel string table, appended in the same call that adds the
//  frame so the two cannot drift.
//
//  KNOWN LIMITATION, to be resolved in phase 8 rather than discovered then:
//  this costs one name/text pair per emitted field for the whole capture, which
//  on a long capture is real memory. It falls on the legacy Frame/bubble path
//  only -- FrameV2 hands its strings to Logic 2 at emission time and retains
//  nothing -- so phase 8 can shrink this while it is naming things anyway.
// ---------------------------------------------------------------------------

class EspiAnalyzerResults : public AnalyzerResults
{
  public:
    EspiAnalyzerResults();
    ~EspiAnalyzerResults() override;

    // The only supported way to add a frame. Keeping the text and the frame in
    // one call is what makes the parallel vector safe.
    //
    // `text` is the field's own value and `detail` is its explanatory children
    // folded into a line. They are kept apart rather than concatenated because
    // a bubble has to be readable at several widths: see GenerateBubbleText.
    U64 AddDecodedField( const Frame& frame, const std::string& name, const std::string& text,
                         const std::string& detail );

    void GenerateBubbleText( U64 frame_index, Channel& channel, DisplayBase display_base ) override;
    void GenerateExportFile( const char* file, DisplayBase display_base, U32 export_type_user_id ) override;
    void GenerateFrameTabularText( U64 frame_index, DisplayBase display_base ) override;
    void GeneratePacketTabularText( U64 packet_id, DisplayBase display_base ) override;
    void GenerateTransactionTabularText( U64 transaction_id, DisplayBase display_base ) override;

  private:
    struct FieldText
    {
        std::string name;
        std::string text;
        std::string detail;
    };

    const FieldText* TextFor( U64 frame_index ) const;

    // The frame's raw value formatted in the user's chosen base.
    std::string NumberText( U64 frame_index, DisplayBase display_base );

    std::vector<FieldText> mText;
};

} // namespace espi_saleae

#endif // ESPI_ANALYZER_RESULTS_H
