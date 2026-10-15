# Pocket Library

The brief. It follows the owner's direction of 2026-10-15, recorded with its
reconciliation in the private planning repository `falseywinchnet/pocket-bible`
(`planning/OWNER_DIRECTION_2026-10-15.md`, `planning/RECONCILIATION_004.md`).

## The game

A bookcase of good books to read, each set as a printed book on India paper. Choose a
book from the shelf and read it a spread at a time; it keeps your place.

## Rule 2

New to the collection: not a game but a reading room, because reading can be just as
fun and is just as useful as playing games. Its decisions are the Pocket Bible's: the
books are set as books (justified, a drop numeral at a chapter, notes at the foot of
the page), not as a scrolling text, on paper you notice only when you look for it.

## A session

The first minute: the shelf, a book taken down, the first spread. A session is as long
as the reader likes. Returning, the reader finds the book they had open, at the spread
they left.

## Replayability

A library is read, not replayed. Every book keeps its own place, and more books come
as they are imported (ebooks and PDFs are planned).

## Passive mode

None is needed: nothing moves unless the reader turns a page.

## Rules

There are none to referee. What must hold: the text of every edition is exactly its
source's, read from a hash-pinned file by a deterministic importer
(`tools/import_books.py`); every correction of a source's own defects is recorded
(`tools/bsb_corrections.tsv`, `tools/avot_corrections.tsv`, and the counted rules in
each book's head); the library file checks every book against its SHA-256 when it is
opened.

## Look, sound and words

The shelf is PlaySuite's own: its walnut wall and oak plank. The books stand spine out,
bound in leather or cloth and lettered in gold. A book opens to a spread on the paper
engine's India paper (#f0ece0) with a soft shadow into the gutter. The Bible is set in
EB Garamond, every other book in Literata, in warm black ink, the words of Christ in
red. The sounds planned are a book taken down and put back and a page turned; none is in
yet, because a sound is judged by listening. No music.

## Controls

Pointer: click a spine to open a book; click the right page to turn forward, the left
to turn back; the wheel turns pages. Keyboard: arrows choose a book on the shelf and
Enter opens it; in a book, arrows, Page Up, Page Down and Space turn pages, C shows
the contents, Escape closes the contents or the book. Capsule: Shelf, Contents,
Previous page, Next page, Help. Settings: the type size.

## Not included

- Search of any kind: the owner removed it (2026-10-15).
- Notes, commentary and study aids beyond what each edition prints itself.
- Hebrew and Greek script: the library sets English only for now; Strong's words are
  given in transliteration.
- Books still under copyright: only what may be copied whole is shelved.
