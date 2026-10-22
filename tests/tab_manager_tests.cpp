#include "session/TabManager.h"

#include <wx/filename.h>
#include <wx/init.h>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "tab_manager_tests: " << message << '\n';
        ++failures;
    }
}

wxString TestPath(const char* name) {
    return wxFileName(wxGetCwd(), name).GetFullPath();
}

void SetScroll(litemark::TabManager& manager, int index, int position) {
    manager.Activate(static_cast<std::size_t>(index));
    manager.selectedMutable()->scrollY = position;
}

void TestNewTabStartsAtTop() {
    litemark::TabManager manager;
    Check(manager.OpenFile(TestPath("alpha.md")) ==
              litemark::TabOpenResult::Opened,
          "first file should open a new tab");
    Check(manager.tabs().size() == 1, "first file should create one tab");
    Check(manager.selectedIndex() == 0, "first tab should be selected");
    Check(manager.selected() != nullptr, "first tab should have a session");
    Check(manager.selected()->scrollY == 0,
          "new tab should start at scroll position zero");
}

void TestExistingFileActivatesWithoutLosingScroll() {
    litemark::TabManager manager;
    const wxString alpha = TestPath("alpha.md");
    manager.OpenFile(alpha);
    SetScroll(manager, 0, 540);
    manager.OpenFile(TestPath("beta.md"));

    Check(manager.OpenFile(alpha) ==
              litemark::TabOpenResult::Activated,
          "opening an existing path should activate its tab");
    Check(manager.tabs().size() == 2,
          "opening an existing path must not create a third tab");
    Check(manager.selectedIndex() == 0,
          "activation should select the existing tab");
    Check(manager.selected()->scrollY == 540,
          "activation should preserve the existing tab scroll position");
}

void TestEquivalentPathActivatesExistingTab() {
    litemark::TabManager manager;
    const wxString alpha = TestPath("alpha.md");
    manager.OpenFile(alpha);
    const wxString separator(1, wxFileName::GetPathSeparator());
    const wxString alias = wxFileName(alpha).GetPathWithSep() + "." +
                           separator + wxFileName(alpha).GetFullName();

    Check(manager.OpenFile(alias) ==
              litemark::TabOpenResult::Activated,
          "equivalent normalized paths should activate the existing tab");
    Check(manager.tabs().size() == 1,
          "equivalent paths must not create duplicate tabs");
}

void TestIndependentScrollPositionsSurviveSwitching() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.OpenFile(TestPath("beta.md"));
    SetScroll(manager, 0, 420);
    SetScroll(manager, 1, 780);

    manager.Activate(0);
    Check(manager.selected()->scrollY == 420,
          "first tab should restore its own scroll position");
    manager.Activate(1);
    Check(manager.selected()->scrollY == 780,
          "second tab should restore its own scroll position");
}

void TestClosingSelectedTabChoosesNextAndPreservesItsState() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.OpenFile(TestPath("beta.md"));
    manager.OpenFile(TestPath("gamma.md"));
    SetScroll(manager, 1, 330);
    SetScroll(manager, 2, 910);

    manager.Activate(1);
    manager.CloseSelected();
    Check(manager.tabs().size() == 2, "closing should remove one tab");
    Check(manager.selectedIndex() == 1,
          "closing the middle tab should select the next tab");
    Check(manager.selected()->title == "gamma.md",
          "the next logical tab should remain selected");
    Check(manager.selected()->scrollY == 910,
          "the newly selected tab should keep its scroll position");
}

void TestClosingBeforeSelectionKeepsSelectedTab() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.OpenFile(TestPath("beta.md"));
    manager.OpenFile(TestPath("gamma.md"));
    SetScroll(manager, 2, 610);
    manager.Activate(2);

    manager.CloseAt(0);
    Check(manager.selectedIndex() == 1,
          "selected index should shift after closing an earlier tab");
    Check(manager.selected()->title == "gamma.md",
          "closing an earlier tab must not change the logical selection");
    Check(manager.selected()->scrollY == 610,
          "selection state should survive closing an earlier tab");
}

void TestNextTabWraps() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.OpenFile(TestPath("beta.md"));
    manager.OpenFile(TestPath("gamma.md"));

    manager.ActivateNext();
    Check(manager.selectedIndex() == 0,
          "next tab should wrap from the last tab to the first");
}

void TestReloadKeepsScrollAndInvalidatesParseGeneration() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    SetScroll(manager, 0, 275);
    const std::uint64_t generation = manager.selected()->generation;
    wxString error;

    Check(manager.ReloadSelected(&error) ==
              litemark::TabOpenResult::Opened,
          "reload should start a new parse");
    Check(manager.selected()->generation == generation + 1,
          "reload should increment the parse generation");
    Check(manager.selected()->loadState ==
              litemark::DocumentLoadState::Loading,
          "reload should return the tab to the loading state");
    Check(manager.selected()->scrollY == 275,
          "reload should preserve the current scroll position");
}

void TestInvalidCloseIsIgnored() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.CloseAt(9);
    Check(manager.tabs().size() == 1, "invalid close must not remove a tab");
    Check(manager.selectedIndex() == 0,
          "invalid close must not change the selection");
}

void TestCompleteParsePublishesDocument() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    const auto tab = *manager.selected();
    const auto document = std::make_shared<litemark::MarkdownDocument>();

    Check(manager.CompleteParse(tab.id, tab.generation, "# alpha", document, ""),
          "matching parse completion should be accepted");
    Check(manager.selected()->loadState ==
              litemark::DocumentLoadState::Ready,
          "completed parse should mark the tab ready");
    Check(manager.selected()->document == document,
          "completed parse should publish the document");
    Check(manager.selected()->content == "# alpha",
          "completed parse should retain source content");
}

void TestStaleParseResultIsIgnored() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    const std::uint64_t oldId = manager.selected()->id;
    const std::uint64_t oldGeneration = manager.selected()->generation;
    manager.ReloadSelected();
    const auto staleDocument = std::make_shared<litemark::MarkdownDocument>();

    Check(!manager.CompleteParse(oldId, oldGeneration, "stale",
                                 staleDocument, ""),
          "stale generation should be rejected");
    Check(manager.selected()->loadState ==
              litemark::DocumentLoadState::Loading,
          "stale result must not complete the new generation");
    Check(manager.selected()->document == nullptr,
          "stale result must not publish a document");
}

void TestReloadFailureKeepsPreviousDocument() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    const auto initial = *manager.selected();
    const auto oldDocument = std::make_shared<litemark::MarkdownDocument>();
    manager.CompleteParse(initial.id, initial.generation, "# old",
                          oldDocument, "");
    manager.ReloadSelected();
    const auto current = *manager.selected();

    Check(manager.CompleteParse(current.id, current.generation, "",
                                nullptr, "decode failed"),
          "failure for the current generation should be recorded");
    Check(manager.selected()->loadState ==
              litemark::DocumentLoadState::Ready,
          "failed reload should keep the previous document readable");
    Check(manager.selected()->document == oldDocument,
          "failed reload must preserve the old document pointer");
    Check(manager.selected()->parseError == "decode failed",
          "failed reload should retain its error message");
}

void TestExternalChangeAndDismiss() {
    litemark::TabManager manager;
    const wxString path = TestPath("alpha.md");
    manager.OpenFile(path);

    Check(manager.MarkExternalChange(path),
          "known file path should be marked as externally changed");
    Check(manager.selected()->externalChanged,
          "external change flag should be set");
    manager.DismissChangeNotice();
    Check(manager.selected()->changeNoticeDismissed,
          "change notice should be dismissible");
    Check(!manager.MarkExternalChange(TestPath("unknown.md")),
          "unknown file path should not affect open tabs");
}

void TestClosingLastTabClearsSelection() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.CloseSelected();
    Check(manager.tabs().empty(), "closing the last tab should empty the list");
    Check(manager.selectedIndex() == -1,
          "closing the last tab should clear the selected index");
    Check(manager.selected() == nullptr,
          "closing the last tab should clear the selection");
}

void TestInvalidActivationIsIgnored() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.OpenFile(TestPath("beta.md"));
    manager.Activate(999);
    Check(manager.selectedIndex() == 1,
          "invalid activation must not change the selected tab");
}

void TestClosingLastTabSelectsPreviousTab() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.OpenFile(TestPath("beta.md"));
    manager.OpenFile(TestPath("gamma.md"));
    SetScroll(manager, 1, 450);
    manager.Activate(1);
    manager.CloseAt(2);
    Check(manager.selected()->title == "beta.md",
          "closing the final tab should keep the previous selection");
    Check(manager.selected()->scrollY == 450,
          "previous tab state should survive closing the final tab");
}

void TestCompletionAfterCloseIsRejected() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    const auto tab = *manager.selected();
    manager.CloseSelected();
    const auto document = std::make_shared<litemark::MarkdownDocument>();
    Check(!manager.CompleteParse(tab.id, tab.generation, "# late",
                                 document, ""),
          "completion after closing a tab should be rejected");
    Check(manager.tabs().empty(),
          "late completion must not recreate a closed tab");
}

void TestBackgroundCompletionDoesNotChangeSelection() {
    litemark::TabManager manager;
    manager.OpenFile(TestPath("alpha.md"));
    manager.OpenFile(TestPath("beta.md"));
    const auto alpha = manager.tabs().front();
    const auto document = std::make_shared<litemark::MarkdownDocument>();
    Check(manager.CompleteParse(alpha.id, alpha.generation, "# alpha",
                                document, ""),
          "background tab completion should be accepted");
    Check(manager.selectedIndex() == 1,
          "background completion must not activate another tab");
    Check(manager.tabs().front().loadState ==
              litemark::DocumentLoadState::Ready,
          "background tab should still become ready");
}

void TestReloadWithoutSelectedTabFails() {
    litemark::TabManager manager;
    wxString error;
    Check(manager.ReloadSelected(&error) ==
              litemark::TabOpenResult::Failed,
          "reload without an open tab should fail");
    Check(!error.empty(), "failed reload should report an error");
}

}  // namespace

int main() {
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "tab_manager_tests: wxWidgets initialization failed\n";
        return EXIT_FAILURE;
    }

    TestNewTabStartsAtTop();
    TestExistingFileActivatesWithoutLosingScroll();
    TestEquivalentPathActivatesExistingTab();
    TestIndependentScrollPositionsSurviveSwitching();
    TestClosingSelectedTabChoosesNextAndPreservesItsState();
    TestClosingBeforeSelectionKeepsSelectedTab();
    TestNextTabWraps();
    TestReloadKeepsScrollAndInvalidatesParseGeneration();
    TestInvalidCloseIsIgnored();
    TestCompleteParsePublishesDocument();
    TestStaleParseResultIsIgnored();
    TestReloadFailureKeepsPreviousDocument();
    TestExternalChangeAndDismiss();
    TestClosingLastTabClearsSelection();
    TestInvalidActivationIsIgnored();
    TestClosingLastTabSelectsPreviousTab();
    TestCompletionAfterCloseIsRejected();
    TestBackgroundCompletionDoesNotChangeSelection();
    TestReloadWithoutSelectedTabFails();

    if (failures != 0) {
        std::cerr << "tab_manager_tests: " << failures
                  << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "tab_manager_tests: 19 cases passed\n";
    return EXIT_SUCCESS;
}
