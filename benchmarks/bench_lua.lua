function bench_all()
    print("=== LUAJIT FULL REPORT ===")
    
    -- 1. Loop Math (1M)
    local s1 = os.clock() * 1000
    local x = 0
    for i = 1, 1000000 do x = x + 1 end
    local e1 = os.clock() * 1000
    print(string.format("1. Loop Math (1M): %.2f ms", e1 - s1))

    -- 2. Raw Array (1M)
    local s2 = os.clock() * 1000
    local arr = {}
    for i = 1, 1000000 do arr[i] = i end
    local sum = 0
    for i = 1, 1000000 do sum = sum + arr[i] end
    local e2 = os.clock() * 1000
    print(string.format("2. Raw Array (1M): %.2f ms", e2 - s2))

    -- 3. Fibonacci(30)
    local function fib(n)
        if n < 2 then return n end
        return fib(n-1) + fib(n-2)
    end
    local s3 = os.clock() * 1000
    fib(30)
    local e3 = os.clock() * 1000
    print(string.format("3. Fibonacci(30): %.2f ms", e3 - s3))
end

bench_all()
