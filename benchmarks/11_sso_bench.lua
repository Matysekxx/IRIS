function bench_sso()
    print("=== LUAJIT SSO TEST ===")
    local start = os.clock() * 1000
    local count = 0
    for i = 0, 999999 do
        local s1 = "id" .. i
        if s1 == "id500000" then
            count = count + 1
        end
    end
    local end_t = os.clock() * 1000
    print(string.format("Result: %.2f ms", end_t - start))
end

bench_sso()
