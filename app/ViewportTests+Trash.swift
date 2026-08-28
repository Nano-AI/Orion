import Foundation

// What travels with a photograph into the Trash - the sibling rules and the
// one-sentence outcome. Pure: the listing is an array of names, so nothing
// here touches a filesystem, and a wrong rule fails as a wrong list rather
// than as somebody's files.
extension ViewportTests {

    static func testTrashPlanPicksExactlyItsSiblings() {
        let photo = URL(fileURLWithPath: "/shoot/IMG_1.arw")
        let listing = ["IMG_1.arw", "IMG_1.xmp", "IMG_1.orion-snapshots.json",
                       "IMG_1.orion-matte-aaaa.png", "IMG_1.orion-matte-bbbb.png",
                       "IMG_2.arw", "IMG_2.xmp", "notes.txt"]
        let plan = TrashPlan.plan(for: photo, directoryListing: listing)

        let names = plan.siblings.map(\.lastPathComponent).sorted()
        report(names == ["IMG_1.orion-matte-aaaa.png", "IMG_1.orion-matte-bbbb.png",
                         "IMG_1.orion-snapshots.json", "IMG_1.xmp"],
               "the sidecar, the versions and every matte travel, nothing else",
               "\(names)")
        report(plan.photo == photo, "the photograph itself leads the plan")
        report(plan.siblings.allSatisfy {
                   $0.deletingLastPathComponent().path == "/shoot"
               },
               "every sibling is in the photograph's own folder")
    }

    static func testTrashPlanDoesNotCrossFilenames() {
        // IMG_1 must not sweep up IMG_10's files - the dot before each
        // marker is what protects this, and this is the pin on it.
        let photo = URL(fileURLWithPath: "/shoot/IMG_1.arw")
        let listing = ["IMG_1.arw", "IMG_10.arw", "IMG_10.xmp",
                       "IMG_10.orion-snapshots.json",
                       "IMG_10.orion-matte-cccc.png",
                       "IMG_1.orion-matte-dddd.png"]
        let plan = TrashPlan.plan(for: photo, directoryListing: listing)

        report(plan.siblings.map(\.lastPathComponent)
                   == ["IMG_1.orion-matte-dddd.png"],
               "IMG_1's plan never touches IMG_10's files",
               "\(plan.siblings.map(\.lastPathComponent))")
    }

    static func testTrashPlanWithNoSiblingsIsEmpty() {
        let photo = URL(fileURLWithPath: "/shoot/IMG_3.arw")
        let plan = TrashPlan.plan(for: photo,
                                  directoryListing: ["IMG_3.arw", "IMG_4.arw"])
        report(plan.siblings.isEmpty,
               "an unedited photograph travels alone, and that is not a failure")
    }

    static func testTrashPlanIgnoresMatteImposters() {
        let photo = URL(fileURLWithPath: "/shoot/IMG_5.arw")
        let listing = ["IMG_5.arw",
                       "IMG_5.orion-matte-eeee.png.bak",   // wrong suffix
                       "IMG_5.orion-matte-ffff.jpg"]       // wrong extension
        let plan = TrashPlan.plan(for: photo, directoryListing: listing)
        report(plan.siblings.isEmpty,
               "a matte is a .png with the prefix, and nothing else qualifies",
               "\(plan.siblings.map(\.lastPathComponent))")
    }

    static func testTrashOutcomeSentences() {
        let a = URL(fileURLWithPath: "/shoot/IMG_1.arw")
        let b = URL(fileURLWithPath: "/shoot/IMG_2.arw")

        let clean = TrashPlan.Outcome.summarize(attempted: 2, trashed: [a, b],
                                                failures: [])
        report(clean.complaint == nil, "nothing failed, nothing said")

        let one = TrashPlan.Outcome.summarize(
            attempted: 3, trashed: [a, b],
            failures: [("IMG_4.arw", "the card is read-only")])
        report(one.complaint == "Moved 2 of 3 photos to the Trash. "
                   + "IMG_4.arw could not be moved - the card is read-only.",
               "a failed photograph is named, with its reason",
               one.complaint ?? "nil")

        // A sibling failing while its photograph moved: the moved count is
        // the photographs that went, so the sentence cannot claim a raw is
        // still here when only its matte is.
        let sibling = TrashPlan.Outcome.summarize(
            attempted: 2, trashed: [a, b],
            failures: [("IMG_1.orion-matte-aaaa.png", "in use")])
        report(sibling.complaint?.hasPrefix("Moved 2 of 2 photos") == true,
               "a stranded sibling does not miscount the photographs",
               sibling.complaint ?? "nil")

        let single = TrashPlan.Outcome.summarize(
            attempted: 1, trashed: [],
            failures: [("IMG_9.arw", "no Trash on this volume")])
        report(single.complaint == "Moved 0 of 1 photo to the Trash. "
                   + "IMG_9.arw could not be moved - no Trash on this volume.",
               "one photograph declines the plural",
               single.complaint ?? "nil")
    }
}
