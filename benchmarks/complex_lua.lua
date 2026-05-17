function matrix_mult(n)
    local A, B, C = {}, {}, {}
    for i = 1, n do
        A[i], B[i], C[i] = {}, {}, {}
        for j = 1, n do
            A[i][j] = i + j - 2
            B[i][j] = (i - 1) * (j - 1)
            C[i][j] = 0.0
        end
    end

    local start = os.clock()
    for i = 1, n do
        for j = 1, n do
            local s = 0.0
            for k = 1, n do
                s = s + A[i][k] * B[k][j]
            end
            C[i][j] = s
        end
    end
    return (os.clock() - start) * 1000
end

function sieve(limit)
    local start = os.clock()
    local primes = {}
    for i = 0, limit do primes[i] = true end
    primes[0], primes[1] = false, false
    for p = 2, math.sqrt(limit) do
        if primes[p] then
            for i = p * p, limit, p do
                primes[i] = false
            end
        end
    end
    local count = 0
    for i = 0, limit do if primes[i] then count = count + 1 end end
    return (os.clock() - start) * 1000
end

print(string.format("Matrix Mult (100x100): %.2f ms", matrix_mult(100)))
print(string.format("Sieve of Erato. (1M): %.2f ms", sieve(1000000)))
