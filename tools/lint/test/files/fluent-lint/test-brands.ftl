# These are brands used in the fluent-lint test

-brand-first = Firefox
-brand-second = Thunderbird
-brand-third = Mozilla
-brand-plural =
    { $plural-form ->
        [true] Smart Windows
       *[false] Smart Window
    }
-brand-unsupported =
    Unsupported { $case ->
        [first] Alpha
       *[other] Beta
    }
-brand-nested =
    { $case ->
        [first]
            { $plural ->
                [true] Gammas
               *[false] Gamma
            }
       *[other] Delta
    }
-brand-reference = { -brand-first } Epsilon
-brand-variant-reference =
    { $case ->
        [first] { -brand-first } Zeta
       *[other] Eta
    }
