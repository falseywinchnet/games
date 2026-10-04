#include "help_content.hpp"
namespace games {
std::string_view help_text(Entry entry) {
    switch (entry) {
    case Entry::solitaire:
        return "Build each foundation from ace through king in one suit. In the seven tableau "
               "columns, build downward in alternating red and black. Move a correctly ordered "
               "face-up run together. Only a king may fill an empty column. Hidden cards turn "
               "over automatically when exposed.\n\n"
               "Click the stock to turn one card, or three in Draw Three mode. The waste "
               "accumulates underneath its visible cards. Only its top card is playable. "
               "Recycle the waste by clicking the empty stock; there is no redeal limit. "
               "Clear all 52 cards to win.\n\n"
               "Click a card then its destination, or drag a legal run. Double-click sends a "
               "card to a foundation. Gold outlines show selection and legal destinations. "
               "Arrows choose a pile/card, Enter selects or places, Space draws, Z undoes. "
               "Use Hint for a suggested legal move; a hint is not a promise of a win. Undo "
               "retains the last 512 actions. Options changes the card back and deal rules.";
    case Entry::spider:
        return "Build downward from king to ace. A card may be placed on the next higher rank "
               "regardless of suit, but only a descending run of one suit moves together. Any "
               "card or valid run may fill an empty column.\n\n"
               "A complete king-to-ace run of one suit moves home automatically. Clear eight "
               "runs to win. Click the stock to deal one card onto every column; every column "
               "must contain a card first. Options offers one, two or four suits and starts a "
               "new deal when this rule changes.\n\n"
               "Click a card then its destination, or drag a legal run. Arrows select, Enter "
               "picks up or places, Space deals, Z undoes. The visible Hint action suggests a "
               "move.";
    case Entry::freecell:
        return "Build four foundations upward from ace to king in one suit. Build tableau "
               "columns downward in alternating colors. An empty free cell holds one card; "
               "an empty tableau column can hold any card.\n\n"
               "An ordered run can move when the empty free cells and columns provide enough "
               "temporary storage to move it one card at a time. Freeing space increases how "
               "many cards you can carry. Move all 52 cards to the foundations to win.\n\n"
               "Click a card then its destination, or drag a legal run. Double-click moves a "
               "card to a foundation. Arrows select, Enter picks up or places, Z undoes. "
               "Hint is available in the command bar. Deal numbers are PlaySuite's own, "
               "not Microsoft deal numbers.";
    case Entry::hearts:
        return "Avoid penalty cards: hearts cost one point each and the queen of spades costs "
               "13. The lowest score wins when someone reaches 100.\n\n"
               "Pass three cards left, right, then across; every fourth hand has no pass. "
               "Select three cards and choose Pass cards. The two of clubs leads the first "
               "trick. Follow suit if possible. The highest card of the led suit wins; aces "
               "are high.\n\n"
               "Hearts cannot lead until a heart has been discarded, unless your hand contains "
               "only hearts. On the first trick you cannot discard points if you have another "
               "choice. Taking all 26 points shoots the moon: each opponent receives 26 instead. "
               "Click a legal card to play. After thirteen tricks choose Next hand.\n\n"
               "The three computer opponents infer hidden cards from play; their memory and "
               "strength vary. They do not need a network connection.";
    case Entry::sudoku:
        return "Fill every row, column and 3 × 3 box with the digits 1–9, once each. Choose "
               "a digit below the board and click a cell to place it; right-click to add a note. "
               "Given digits cannot be changed.\n\n"
               "Arrows move the selection, number keys enter digits, N toggles notes, "
               "Backspace erases and Z undoes. Mistakes remain counted after undo. Hover a "
               "filled square to highlight its digit, row and column.\n\n"
               "The puzzle saves after each change. Difficulty selects the next puzzle; "
               "New game creates a unique puzzle offline. Top scores keep ten names per "
               "difficulty, ranked by fewest mistakes.";
    case Entry::gems:
        return "Click a gem then a neighbor to swap them, or drag the gem itself. Make a line "
               "of three or more of the same color. A swap without a match slides back. "
               "Wait and an available pair will nudge.\n\n"
               "Four in a line makes a colored bomb; matching it clears a 3 × 3 area. "
               "A T or L match makes a star; matching it clears its row and column. "
               "Five in a line makes a hypercube; swap it with a color to clear that color.\n\n"
               "Blasts trigger other specials. Cascades refill the board from above. "
               "Additional colors arrive as you progress. The run ends when no swap is available.";
    case Entry::cube:
        return "Connect every colored pair across the three visible faces. Paths cannot "
               "share a square or cross a stone. Easy has five pairs; Medium has seven "
               "and mossy stones; Hard has nine and also requires every open square to be "
               "filled.\n\n"
               "Move the pointer to tilt the cube. Click an endpoint and trace a path; the "
               "cube holds still while you trace. Start at an endpoint to redraw that pair, "
               "or trace backward to erase. The Next setting chooses the next level. "
               "Every board has a full solution.";
    case Entry::untangle:
        return "Drag the pegs until no thread crosses another thread of its own color. "
               "Different colors may cross. Snags show the tangles that remain. Keep pegs "
               "apart: a peg sitting on a thread also counts as a tangle.\n\n"
               "Frosted pegs cannot move until all their attached threads are clear. "
               "The cat sits on pegs and chases what you drag. Click it to shoo it. "
               "On Medium and Hard, leaving the yarn alone may invite a swat at a peg.\n\n"
               "The Next setting chooses the next level. Every puzzle starts from a "
               "known untangled arrangement.";
    case Entry::atom:
        return "Four atoms hide in the fogged chamber. Click an emitter on the rim to fire "
               "a beam and observe where it emerges.\n\n"
               "Straight into an atom: absorbed (H). An atom diagonally ahead bends the beam "
               "90 degrees away. Atoms on both forward diagonals send it straight back (R). "
               "An atom beside the entry square also reflects it at the door. Otherwise the "
               "entry and exit ports receive the same number.\n\n"
               "Click the glass to place markers; right-click or Control-click crosses a "
               "square out. Place four markers and pull the lever, or press Enter. "
               "Each H or R costs one point; each detour costs two. Find the atoms for "
               "the fewest points. Every box can be worked out exactly. N starts a new "
               "box and T opens top scores.";
    case Entry::pegs:
        return "Find the secret code of four pegs chosen from six colors. Colors may repeat. "
               "The code stays fixed throughout the game.\n\n"
               "Drag pegs into sockets, select a peg then a socket, or select a socket then a "
               "peg. A peg can fill the next socket; click a placed peg to remove it. "
               "Press the red CHECK button or Enter when the row is complete.\n\n"
               "Gold pins mean a correct color in the correct position. White rings mean "
               "a correct color in the wrong position. Feedback does not identify a particular "
               "peg, and each occurrence is counted once. Solve within ten attempts; fewer "
               "attempts make a better score.\n\n"
               "Keys 1–6 place colors, Backspace removes, Enter checks, N starts a new "
               "game and T opens top scores. The test sheet records your attempts.";
    case Entry::switchbox:
        return "She lives in the box, and those are her switches. Flip one and she pops up "
               "to push it down. The box knows a secret order for all six switches. "
               "Flip the correct next switch and its lamp lights; a wrong one extinguishes "
               "every lamp.\n\n"
               "Light all six to crack the combination. Fewer flips make a better score. "
               "Then she chooses a new combination. Flip quickly and she must hurry—she has "
               "two hands. Click a switch or press 1–6. T opens top scores.";
    case Entry::solve:
        return "Rebuild the blue and yellow picture using all seven pieces. Drag a piece "
               "from the tray into the frame; it snaps into place.\n\n"
               "While carrying a piece, R or a right-click rotates it, and F mirrors it. "
               "Click the selected piece in the tray to turn it before dragging. Drag a "
               "placed piece to move it; right-click a placed piece to return it.\n\n"
               "The target comes from an actual tiling. An alternative arrangement with "
               "the same colors is also a solution.";
    case Entry::eggy:
        return "Eggy is climbing the Very, Very Tall Mountain. It is very, very tall. "
               "Arrow keys or W A S D steer, with Up pointing uphill; Space hops. You can "
               "also hold the pointer on the ground to guide him.\n\n"
               "Stop helping and he climbs by himself, a little slower. He never gives up "
               "and keeps climbing while the game is closed. Ice is slippery: look for "
               "stone ledges. Hop logs and rocks. Leaves can bonk him, wind slows him and "
               "water refreshes him. Floating stars require a helping hand; the autopilot "
               "will not chase them.\n\n"
               "T opens top scores; + and − change zoom. Why can't Eggy be your screensaver? "
               "Because the ducks always find their way onto people's desks.";
    case Entry::koikoi:
        return "The deck has twelve months of four cards, each month represented by a flower. "
               "Play a card from your hand. Match its month to a field card to take both. "
               "Choose which if two match; take all four if three match. Otherwise your card "
               "joins the field. Then the top draw-pile card turns over and matches the same "
               "way.\n\n"
               "Collect scoring sets, listed in Koi-Koi — the sets below. After making a new "
               "set, stop to score the round or call koi-koi to continue for more. If your "
               "opponent then makes a set and stops, their points double because you called. "
               "A round worth seven or more points doubles. Four cards of one month or four "
               "pairs in the opening hand wins six points outright.\n\n"
               "A match lasts twelve rounds. The round winner deals and starts the next. "
               "Keys 1–8 play cards, K calls koi-koi, S stops and Enter continues. "
               "Use the visible Hint action for advice. Score shows the current match.";
    case Entry::parrots:
        return "Every parrot is either honest, with every statement true, or a liar, with "
               "every statement false. You know exactly how many are lying. One did the deed.\n\n"
               "Click a beak to mark its bird honest (green), lying (red), or unmarked. "
               "Statements that cannot hold under your marks turn red. Some birds need "
               "a question before volunteering information. You have only one or two questions; "
               "choose ones that distinguish the possibilities.\n\n"
               "When sure, name the culprit. Every table is solvable by reasoning alone. "
               "Keys 1–7 mark birds; Enter continues. Records shows your results.";
    case Entry::liarsdice:
        return "Everyone starts with five dice hidden under a cup. A bid claims how many "
               "dice of a face exist across the whole table: seven fours means at least "
               "seven fours. Ones are wild and count as every face; nobody bids on ones.\n\n"
               "Raise by bidding more dice, or the same quantity of a higher face. "
               "Instead, call the previous bid a lie: every cup lifts. If the bid stands, "
               "the caller loses a die; otherwise the bidder loses one. Lost dice go in "
               "the Keeper's jar. Lose all your dice to lose the wager; be last with dice to "
               "win.\n\n"
               "The crew have different bluffing habits and tells. Some learn your habits, "
               "too. Keys 2–6 choose the face, Up/Down changes quantity, Enter bids and "
               "L calls liar. The Crew logbook and Records show your current discoveries "
               "and history; wager cards state what each game risks and rewards.";
    case Entry::penthesheep:
        return "Click a grass patch to build a fence; it joins neighboring fences. Then "
               "the sheep takes one step toward the edge. If it reaches the edge, it escapes. "
               "Block every route out and it sits down to sulk: you win. You have three "
               "fences before the sheep starts moving.\n\n"
               "Dozy sheep dawdle, clever sheep preserve their options, and cunning sheep "
               "think a fence ahead. Every sheep stops for clover. Pen it at or under "
               "par—the number of fences our shepherd needed—to earn three stars.\n\n"
               "Z undoes, R restarts and L opens Meadows. Use the visible Hint action "
               "for a suggested fence.";
    case Entry::rockstack:
        return "Stack rocks as tall as they will stand. Height measures the first rock set "
               "down and the rocks resting on it. Click a rock in the bowl and Mina's crane "
               "fetches it.\n\n"
               "Up/Down extends or retracts the boom; Left/Right swings it. W/S raises or "
               "lowers the line. Q/E turns the rock, R/F tips it, and Z/C rolls it. "
               "Hold Shift for fine adjustments.\n\n"
               "Lower slowly onto the stack. As the rock settles, the slings go slack. "
               "When slack and still, press Space to let go. B carries the rock back "
               "to the bowl. Click the top rock to dismantle a stack. Fallen rocks are "
               "tidied back into the bowl.\n\n"
               "Drag to look around and scroll to zoom. Every site is saved; Sites "
               "returns to earlier sites. Hold Start over to return all rocks to the bowl.";
    }
    return {};
}
} // namespace games
