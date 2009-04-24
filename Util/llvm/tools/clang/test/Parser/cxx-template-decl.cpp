// RUN: clang -fsyntax-only -verify %s

// Errors
export class foo { };   // expected-error {{expected template}}
template  x;            // expected-error {{expected '<' after 'template'}}
export template x;      // expected-error {{expected '<' after 'template'}} \
                        // expected-note {{exported templates are unsupported}}
// See Sema::ParsedFreeStandingDeclSpec about the double diagnostic. This is
// because ParseNonTypeTemplateParameter starts parsing a DeclSpec.
template < ;            // expected-error {{parse error}} expected-error {{declaration does not declare anything}}
template <template X> struct Err1; // expected-error {{expected '<' after 'template'}}
template <template <typename> > struct Err2;       // expected-error {{expected 'class' before '>'}}
template <template <typename> Foo> struct Err3;    // expected-error {{expected 'class' before 'Foo'}}

// Template function declarations
template <typename T> void foo();
template <typename T, typename U> void foo();

// Template function definitions.
template <typename T> void foo() { }

// Template class (forward) declarations
template <typename T> struct A;
template <typename T, typename U> struct b;
template <typename> struct C;
template <typename, typename> struct D;

// Forward declarations with default parameters?
template <typename T = int> X1;
template <typename = int> X2;

// Forward declarations w/template template parameters
template <template <typename> class T> class TTP1;
template <template <typename> class> class TTP2;
template <template <typename> class T = foo> TTP3;
template <template <typename> class = foo> TTP3;
template <template <typename X, typename Y> class T> TTP5;

// Forward declararations with non-type params
template <int> class NTP0;
template <int N> class NTP1;
template <int N = 5> class NTP2;
template <int = 10> class NTP3;
template <unsigned int N = 12u> NTP4;;
template <unsigned int = 12u> NTP5;
template <unsigned = 15u> NTP6;
template <typename T, T Obj> NTP7;

// Template class declarations
template <typename T> struct A { };
template <typename T, typename U> struct B { };

// Template parameter shadowing
template<typename T, // expected-note{{template parameter is declared here}}
	 typename T> // expected-error{{declaration of 'T' shadows template parameter}}
  void shadow1();

template<typename T> // expected-note{{template parameter is declared here}}
void shadow2(int T); // expected-error{{declaration of 'T' shadows template parameter}}

template<typename T> // expected-note{{template parameter is declared here}}
class T { // expected-error{{declaration of 'T' shadows template parameter}}
};

template<int Size> // expected-note{{template parameter is declared here}}
void shadow3(int Size); // expected-error{{declaration of 'Size' shadows template parameter}}

// Non-type template parameters in scope
template<int Size> 
void f(int& i) {
  i = Size;
  Size = i; // expected-error{{expression is not assignable}}
}

template<typename T>
const T& min(const T&, const T&);
