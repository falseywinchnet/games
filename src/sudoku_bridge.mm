#include "sudoku.hpp"
#import <Foundation/Foundation.h>
#import <JavaScriptCore/JavaScriptCore.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace games {
static std::string read_source(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("The bundled Sudoku engine is missing.");
    std::ostringstream source;
    source << file.rdbuf();
    return source.str();
}
Sudoku SudokuJob::operator()() const {
    @autoreleasepool {
        JSContext* context = [[JSContext alloc] init];
        std::string source = read_source(asset_path + "/sudoku/engine.js");
        [context evaluateScript:[NSString stringWithUTF8String:source.c_str()]];
        if (context.exception) {
            std::string error([[context.exception toString] UTF8String]);
            [context release];
            throw std::runtime_error(error);
        }
        std::string calibration = read_source(asset_path + "/sudoku/calibration.json");
        [context
            evaluateScript:[NSString
                               stringWithUTF8String:("const calibration = " + calibration + ";")
                                                        .c_str()]];
        [context setObject:[NSString stringWithUTF8String:seed.c_str()]
            forKeyedSubscript:@"requestedSeed"];
        [context setObject:[NSNumber numberWithInt:difficulty]
            forKeyedSubscript:@"requestedDifficulty"];
        JSValue* value =
            [context evaluateScript:@"JSON.stringify(generate({difficulty:['easy','medium','hard']["
                                    @"requestedDifficulty],seed:requestedSeed,calibration}))"];
        if (context.exception) {
            std::string error([[context.exception toString] UTF8String]);
            [context release];
            throw std::runtime_error(error);
        }
        NSString* json = [value toString];
        NSDictionary* result =
            [NSJSONSerialization JSONObjectWithData:[json dataUsingEncoding:NSUTF8StringEncoding]
                                            options:0
                                              error:nil];
        Sudoku game;
        game.seed = seed;
        game.difficulty = difficulty;
        NSArray* puzzle = [result objectForKey:@"puzzle"];
        NSArray* solution = [result objectForKey:@"solution"];
        if ([puzzle isKindOfClass:[NSArray class]] && [solution isKindOfClass:[NSArray class]] &&
            puzzle.count == 81 && solution.count == 81)
            for (int i = 0; i < 81; ++i) {
                game.puzzle[i] = [[puzzle objectAtIndex:i] intValue];
                game.solution[i] = [[solution objectAtIndex:i] intValue];
                game.grid.values[i] = game.puzzle[i];
            }
        [context release];
        if (!game.invariant())
            throw std::runtime_error("The Sudoku engine returned an invalid puzzle.");
        game.message = "Left click to set · right click to note";
        return game;
    }
}
} // namespace games
