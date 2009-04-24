// RUN: clang -fsyntax-only -verify < %s

%:include <stdio.h>

    %:ifndef BUFSIZE
     %:define BUFSIZE  512
    %:endif

    void copy(char d<::>, const char s<::>, int len)
    <%
        while (len-- >= 0)
        <%
            d<:len:> = s<:len:>;
        %>
    %>
