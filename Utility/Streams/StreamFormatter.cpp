// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StreamFormatter.h"
#include "Stream.h"
#include "../BitUtils.h"
#include "../PtrUtils.h"
#include "../StringFormat.h"
#include "../Conversion.h"
#include "../../Core/Exceptions.h"
#include <assert.h>
#include <algorithm>

namespace Utility
{
    void MemoryMappedInputStream::MovePointer(ptrdiff_t offset) 
    { 
        assert(PtrAdd(_ptr, offset) <= _end && PtrAdd(_ptr, offset) >= _start);
        _ptr = PtrAdd(_ptr, offset);
    }

    void MemoryMappedInputStream::SetPointer(const void* newPtr)
    {
        assert(newPtr <= _end && newPtr >= _start);
        _ptr = newPtr;
    }

    MemoryMappedInputStream::MemoryMappedInputStream(const void* start, const void* end) 
    {
        _start = _ptr = start;
        _end = end;
    }

    MemoryMappedInputStream::~MemoryMappedInputStream() {}

    static const unsigned TabWidth = 4;
    
    template<typename CharType>
        struct FormatterConstants 
    {
        static const CharType EndLine[];
        static const CharType Tab;
        static const CharType ElementPrefix;
        
        static const CharType ProtectedNamePrefix[];
        static const CharType ProtectedNamePostfix[];

        static const CharType CommentPrefix[];
        static const CharType HeaderPrefix[];
    };

    template<typename CharType, int Count> 
        static void WriteConst(OutputStream& stream, const CharType (&cnst)[Count], unsigned& lineLength)
    {
        stream.WriteString(cnst, &cnst[Count]);
        lineLength += Count;
    }

    template<typename CharType> 
        static bool FormattingChar(CharType c)
    {
        return c=='~' || c==';' || c=='=' || c=='\r' || c=='\n' || c == 0x0;
    }

    template<typename CharType> 
        static bool WhitespaceChar(CharType c)  // (excluding new line)
    {
        return c==' ' || c=='\t' || c==0x0B || c==0x0C || c==0x85 || c==0xA0 || c==0x0;
    }

    template<typename CharType> 
        static bool IsSimpleString(const CharType* start, const CharType* end)
    {
            // if there are formatting chars anywhere in the string, it's not simple
        if (std::find_if(start, end, FormattingChar<CharType>) != end) return false;

            // If the string beings or ends with whitespace, it is also not simple.
            // This is because the parser will strip off leading and trailing whitespace.
            // (note that this test will also consider an empty string to be "not simple"
        if (start == end) return false;
        if (WhitespaceChar(*start) || WhitespaceChar(*(end-1))) return false;
        if (*start == FormatterConstants<CharType>::ProtectedNamePrefix[0]) return false;
        return true;
    }

    template<typename CharType>
        auto OutputStreamFormatter::BeginElement(const CharType* nameStart, const CharType* nameEnd) -> ElementId
    {
        DoNewLine<CharType>();

        _hotLine = true; DoNewLine<CharType>(); // (force extra new line before new element)

        assert(nameEnd > nameStart);

        _stream->WriteChar(FormatterConstants<CharType>::ElementPrefix);

            // in simple cases, we just write the name without extra formatting 
            //  (otherwise we have to write a string prefix and string postfix
        if (IsSimpleString(nameStart, nameEnd)) {
            _stream->WriteString(nameStart, nameEnd);
        } else {
            WriteConst(*_stream, FormatterConstants<CharType>::ProtectedNamePrefix, _currentLineLength);
            _stream->WriteString(nameStart, nameEnd);
            WriteConst(*_stream, FormatterConstants<CharType>::ProtectedNamePostfix, _currentLineLength);
        }

        _hotLine = true;
        _currentLineLength += unsigned(nameEnd - nameStart + 1);
        ++_currentIndentLevel;

        #if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
            auto id = _nextElementId;
            _elementStack.push_back(id);
            return id;
        #else
            return 0;
        #endif
    }

    template<typename CharType>
        void OutputStreamFormatter::DoNewLine()
    {
        if (_pendingHeader) {
            WriteConst(*_stream, FormatterConstants<CharType>::HeaderPrefix, _currentLineLength);
            StringMeld<128, CharType> buffer;
            buffer << "Format=1; Tab=" << TabWidth;
            _stream->WriteNullTerm(buffer.get());

            _hotLine = true;
            _pendingHeader = false;
        }

        if (_hotLine) {
            WriteConst(*_stream, FormatterConstants<CharType>::EndLine, _currentLineLength);
            
            CharType tabBuffer[64];
            if (_currentIndentLevel > dimof(tabBuffer))
                ThrowException(::Exceptions::BasicLabel("Excessive indent level found in OutputStreamFormatter (%i)", _currentIndentLevel));
            std::fill(tabBuffer, &tabBuffer[_currentIndentLevel], FormatterConstants<CharType>::Tab);
            _stream->WriteString(tabBuffer, &tabBuffer[_currentIndentLevel]);
            _hotLine = false;
            _currentLineLength = _currentIndentLevel * TabWidth;
        }
    }

    template<typename CharType> 
        void OutputStreamFormatter::WriteAttribute(
            const CharType* nameStart, const CharType* nameEnd,
            const CharType* valueStart, const CharType* valueEnd)
    {
        const unsigned idealLineLength = 100;
        bool forceNewLine = 
            (_currentLineLength + (valueEnd - valueStart) + (nameEnd - nameStart) + 3) > idealLineLength;

        if (forceNewLine) {
            DoNewLine<CharType>();
        } else if (_hotLine) {
            _stream->WriteChar((CharType)';');
            _stream->WriteChar((CharType)' ');
            _currentLineLength += 2;
        }

        if (IsSimpleString(nameStart, nameEnd)) {
            _stream->WriteString(nameStart, nameEnd);
        } else {
            WriteConst(*_stream, FormatterConstants<CharType>::ProtectedNamePrefix, _currentLineLength);
            _stream->WriteString(nameStart, nameEnd);
            WriteConst(*_stream, FormatterConstants<CharType>::ProtectedNamePostfix, _currentLineLength);
        }

        _stream->WriteChar((CharType)'=');

        if (IsSimpleString(valueStart, valueEnd)) {
            _stream->WriteString(valueStart, valueEnd);
        } else {
            WriteConst(*_stream, FormatterConstants<CharType>::ProtectedNamePrefix, _currentLineLength);
            _stream->WriteString(valueStart, valueEnd);
            WriteConst(*_stream, FormatterConstants<CharType>::ProtectedNamePostfix, _currentLineLength);
        }

        _currentLineLength += unsigned((valueEnd - valueStart) + (nameEnd - nameStart) + 1);
        _hotLine = true;
    }

    void OutputStreamFormatter::EndElement(ElementId id)
    {
        if (_currentIndentLevel == 0)
            ThrowException(::Exceptions::BasicLabel("Unexpected EndElement in OutputStreamFormatter"));

        #if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
            assert(_elementStack.size() == _currentIndentLevel);
            if (_elementStack[_elementStack.size()-1] != id)
                ThrowException(::Exceptions::BasicLabel("EndElement for wrong element id in OutputStreamFormatter"));
            _elementStack.erase(_elementStack.end()-1);
        #endif

        --_currentIndentLevel;
    }

    void OutputStreamFormatter::Flush()
    {
        DoNewLine<utf8>();  // finish on a new line (just for neatness)
        assert(_currentIndentLevel == 0);
    }

    void OutputStreamFormatter::NewLine()
    {
        DoNewLine<utf8>();
    }

    OutputStreamFormatter::OutputStreamFormatter(OutputStream& stream) 
    : _stream(&stream)
    {
        _currentIndentLevel = 0;
        _hotLine = false;
        _currentLineLength = 0;
        _pendingHeader = true;
    }

    OutputStreamFormatter::~OutputStreamFormatter()
    {}

    template<> auto OutputStreamFormatter::BeginElement(const char* nameStart, const char* nameEnd) -> ElementId
    {
        return BeginElement((const utf8*)nameStart, (const utf8*)nameEnd);
    }

    template<> void OutputStreamFormatter::WriteAttribute(const char* nameStart, const char* nameEnd, const char* valueStart, const char* valueEnd)
    {
        WriteAttribute(
            (const utf8*)nameStart, (const utf8*)nameEnd,
            (const utf8*)valueStart, (const utf8*)valueEnd);
    }

    template auto OutputStreamFormatter::BeginElement(const utf8* nameStart, const utf8* nameEnd) -> ElementId;
    template auto OutputStreamFormatter::BeginElement(const ucs2* nameStart, const ucs2* nameEnd) -> ElementId;
    template auto OutputStreamFormatter::BeginElement(const ucs4* nameStart, const ucs4* nameEnd) -> ElementId;
    
    template void OutputStreamFormatter::WriteAttribute(const utf8* nameStart, const utf8* nameEnd, const utf8* valueStart, const utf8* valueEnd);
    template void OutputStreamFormatter::WriteAttribute(const ucs2* nameStart, const ucs2* nameEnd, const ucs2* valueStart, const ucs2* valueEnd);
    template void OutputStreamFormatter::WriteAttribute(const ucs4* nameStart, const ucs4* nameEnd, const ucs4* valueStart, const ucs4* valueEnd);

    
    FormatException::FormatException(const char label[], StreamLocation location)
        : ::Exceptions::BasicLabel("Format Exception: (%s) at line (%i), char (%i)", label, location._lineIndex, location._charIndex) {}

    template<typename CharType, int Count>
        bool TryEat(MemoryMappedInputStream& stream, CharType (&pattern)[Count])
    {
        if (stream.RemainingBytes() < (sizeof(CharType)*Count))
            return false;

        const auto* test = (const CharType*)stream.ReadPointer();
        for (unsigned c=0; c<Count; ++c)
            if (test[c] != pattern[c])
                return false;
        
        stream.MovePointer(sizeof(CharType)*Count);
        return true;
    }

    template<typename CharType, int Count>
        void Eat(MemoryMappedInputStream& stream, CharType (&pattern)[Count], StreamLocation location)
    {
        if (stream.RemainingBytes() < (sizeof(CharType)*Count))
            ThrowException(FormatException("Blob prefix clipped", lineIndex, charIndex));

        const auto* test = (const CharType*)stream.ReadPointer();
        for (unsigned c=0; c<Count; ++c)
            if (test[c] != pattern[c])
                ThrowException(FormatException("Malformed blob prefix", location));
        
        stream.MovePointer(sizeof(CharType)*Count);
    }

    template<typename CharType>
        const CharType* ReadToStringEnd(
            MemoryMappedInputStream& stream, bool protectedStringMode,
            StreamLocation location)
    {
        const auto pattern = FormatterConstants<CharType>::ProtectedNamePostfix;
        const auto patternLength = dimof(FormatterConstants<CharType>::ProtectedNamePostfix);

        if (protectedStringMode) {
            const auto* end = ((const CharType*)stream.End()) - patternLength;
            const auto* ptr = (const CharType*)stream.ReadPointer();
            while (ptr <= end) {
                for (unsigned c=0; c<patternLength; ++c)
                    if (ptr[c] != pattern[c])
                        goto advptr;

                stream.SetPointer(ptr + patternLength);
                return ptr;
            advptr:
                ++ptr;
            }

            ThrowException(FormatException("String deliminator not found", location));
        } else {
                // we must read forward until we hit a formatting character
                // the end of the string will be the last non-whitespace before that formatting character
            const auto* end = ((const CharType*)stream.End());
            const auto* ptr = (const CharType*)stream.ReadPointer();
            const auto* stringEnd = ptr;
            for (;;) {
                    // here, hitting EOF is the same as hitting a formatting char
                if (ptr == end || FormattingChar(*ptr)) {
                    stream.SetPointer(ptr);
                    return stringEnd;
                } else if (!WhitespaceChar(*ptr)) {
                    stringEnd = ptr+1;
                }
                ++ptr;
            }
        }
    }

    template<typename CharType>
        void EatWhitespace(MemoryMappedInputStream& stream)
    {
            // eat all whitespace (excluding new line)
        const auto* end = ((const CharType*)stream.End());
        const auto* ptr = (const CharType*)stream.ReadPointer();
        while (ptr < end && WhitespaceChar(*ptr)) ++ptr;
        stream.SetPointer(ptr);
    }

    template<typename CharType>
        auto InputStreamFormatter<CharType>::PeekNext() -> Blob
    {
        if (_primed != Blob::None) return _primed;

        using Consts = FormatterConstants<CharType>;
        
        if (_pendingHeader) {
                // attempt to read file header
            if (TryEat(_stream, Consts::HeaderPrefix))
                ReadHeader();

            _pendingHeader = false;
        }

        while (_stream.RemainingBytes() >= sizeof(CharType)) {
            const auto* next = (const CharType*)_stream.ReadPointer();

            switch (*next)
            {
            case '\t':
                _stream.MovePointer(sizeof(CharType));
                _activeLineSpaces = CeilToMultiple(_activeLineSpaces+1, _tabWidth);
                break;
            case ' ': 
                _stream.MovePointer(sizeof(CharType));
                ++_activeLineSpaces; 
                break;

                // throw exception when using an extended unicode whitespace character
                // let's just stick to the ascii whitespace characters for simplicity
            case 0x0B:  // (line tabulation)
            case 0x0C:  // (form feed)
            case 0x85:  // (next line)
            case 0xA0:  // (no-break space)
                ThrowException(FormatException("Unsupported white space character", GetLocation()));

            case '\r':  // (could be an independant new line, or /r/n combo)
                _stream.MovePointer(sizeof(CharType));
                if (    _stream.RemainingBytes() > sizeof(CharType)
                    &&  *(const CharType*)_stream.ReadPointer() == '\n')
                    _stream.MovePointer(sizeof(CharType));

                    // don't adjust _expected line spaces here -- we want to be sure 
                    // that lines with just whitespace don't affect _activeLineSpaces
                _activeLineSpaces = 0;
                ++_lineIndex; _lineStart = _stream.ReadPointer();
                break;

            case '\n':  // (independant new line. A following /r will be treated as another new line)
                _stream.MovePointer(sizeof(CharType));
                _activeLineSpaces = 0;
                ++_lineIndex; _lineStart = _stream.ReadPointer();
                break;

            case ';':
                    // deliminator is ignored here
                _stream.MovePointer(sizeof(CharType));
                break;

            case '=':
                _stream.MovePointer(sizeof(CharType));
                EatWhitespace<CharType>(_stream);
                _protectedStringMode = TryEat(_stream, Consts::ProtectedNamePrefix);
                return _primed = Blob::AttributeValue;

            case '~':
                if (TryEat(_stream, Consts::CommentPrefix)) {
                        // this is a comment... Read forward until the end of the line
                    _stream.MovePointer(2*sizeof(CharType));
                    {
                        const auto* end = ((const CharType*)_stream.End());
                        const auto* ptr = (const CharType*)_stream.ReadPointer();
                        while (ptr < end && *ptr!='\r' && *ptr!='\n') ++ptr;
                        _stream.SetPointer(ptr);
                    }
                    break;
                }

                // else, this is a new element
                if (_activeLineSpaces <= _parentBaseLine) {
                    _protectedStringMode = false;
                    return _primed = Blob::EndElement;
                }

                _stream.MovePointer(sizeof(CharType));
                _protectedStringMode = TryEat(_stream, Consts::ProtectedNamePrefix);
                return _primed = Blob::BeginElement;

            default:
                // first, if our spacing has decreased, then we must consider it an "end element"
                // caller must follow with "TryReadEndElement" until _expectedLineSpaces matches _activeLineSpaces
                if (_activeLineSpaces <= _parentBaseLine) {
                    _protectedStringMode = false;
                    return _primed = Blob::EndElement;
                }

                    // now, _activeLineSpaces must be larger than _parentBaseLine. Anything that is 
                    // more indented than it's parent will become it's child
                    // let's see if there's a fully formed blob here
                _protectedStringMode = TryEat(_stream, Consts::ProtectedNamePrefix);
                return _primed = Blob::AttributeName;
            }
        }

            // we've reached the end of the stream...
            // while there are elements on our stack, we need to end them
        if (_baseLineStackPtr > 0) return _primed = Blob::EndElement;
        return Blob::None;
    }

    template<typename CharType> int AsInt(const CharType* inputStart, const CharType* inputEnd)
    {
        char buffer[32];
        Conversion::Convert(buffer, dimof(buffer), inputStart, inputEnd);
        return XlAtoI32(buffer);
    }

    template<typename CharType>
        void InputStreamFormatter<CharType>::ReadHeader()
    {
        const CharType* aNameStart = nullptr;
        const CharType* aNameEnd = nullptr;

        while (_stream.RemainingBytes() >= sizeof(CharType)) {
            const auto* next = (const CharType*)_stream.ReadPointer();
            switch (*next)
            {
            case '\t':
            case ' ': 
            case ';':
                _stream.MovePointer(sizeof(CharType));
                break;

            case 0x0B: case 0x0C: case 0x85: case 0xA0:
                ThrowException(FormatException("Unsupported white space character", GetLocation()));

            case '~':
                ThrowException(FormatException("Unexpected element in header", GetLocation()));

            case '\r':  // (could be an independant new line, or /r/n combo)
            case '\n':  // (independant new line. A following /r will be treated as another new line)
                return;

            case '=':
                _stream.MovePointer(sizeof(CharType));
                EatWhitespace<CharType>(_stream);
                
                {
                    const auto* aValueStart = (const CharType*)_stream.ReadPointer();
                    const auto* aValueEnd = ReadToStringEnd<CharType>(_stream, false, GetLocation());

                    char convBuffer[12];
                    Conversion::Convert(convBuffer, dimof(convBuffer), aNameStart, aNameEnd);

                    if (!XlCompareStringI(convBuffer, "Format")) {
                        if (AsInt(aValueStart, aValueEnd)!=1)
                            ThrowException(FormatException("Unsupported format in input stream formatter header", GetLocation()));
                    } else if (!XlCompareStringI(convBuffer, "Tab")) {
                        _tabWidth = AsInt(aValueStart, aValueEnd);
                        if (_tabWidth==0)
                            ThrowException(FormatException("Bad tab width in input stream formatter header", GetLocation()));
                    }
                }
                break;

            default:
                aNameStart = next;
                aNameEnd = ReadToStringEnd<CharType>(_stream, false, GetLocation());
                break;
            }
        }
    }

    template<typename CharType>
        bool InputStreamFormatter<CharType>::TryReadBeginElement(InteriorSection& name)
    {
        if (PeekNext() != Blob::BeginElement) return false;

        name._start = (const CharType*)_stream.ReadPointer();
        name._end = ReadToStringEnd<CharType>(_stream, _protectedStringMode, GetLocation());

        // the new "parent base line" should be the indentation level of the line this element started on
        if ((_baseLineStackPtr+1) > dimof(_baseLineStack))
            ThrowException(FormatException(
                "Excessive indentation format in input stream formatter", GetLocation()));

        _baseLineStack[_baseLineStackPtr++] = _activeLineSpaces;
        _parentBaseLine = _activeLineSpaces;
        _primed = Blob::None;
        _protectedStringMode = false;
        return true;
    }

    template<typename CharType>
        bool InputStreamFormatter<CharType>::TryReadEndElement()
    {
        if (PeekNext() != Blob::EndElement) return false;

        if (_baseLineStackPtr != 0) {
            _parentBaseLine = (_baseLineStackPtr > 1) ? _baseLineStack[_baseLineStackPtr-2] : -1;
            --_baseLineStackPtr;
        }

        _primed = Blob::None;
        _protectedStringMode = false;
        return true;
    }

    template<typename CharType>
        void InputStreamFormatter<CharType>::SkipElement()
    {
        unsigned subtreeEle = 0;
        InteriorSection dummy0, dummy1;
        switch(PeekNext()) {
        case Blob::BeginElement:
            if (!TryReadBeginElement(dummy0))
                ThrowException(FormatException(
                    "Malformed begin element while skipping forward", GetLocation()));
            ++subtreeEle;
            break;

        case Blob::EndElement:
            if (!subtreeEle) return;    // end now, while the EndElement is primed

            if (!TryReadEndElement())
                ThrowException(FormatException(
                    "Malformed end element while skipping forward", GetLocation()));
            --subtreeEle;
            break;

        case Blob::AttributeName:
            if (!TryReadAttribute(dummy0, dummy1))
                ThrowException(FormatException(
                    "Malformed attribute while skipping forward", GetLocation()));
            break;

        default:
            ThrowException(FormatException(
                "Unexpected blob or end of stream hit while skipping forward", GetLocation()));
        }
    }

    template<typename CharType>
        bool InputStreamFormatter<CharType>::TryReadAttribute(InteriorSection& name, InteriorSection& value)
    {
        if (PeekNext() != Blob::AttributeName) return false;

        name._start = (const CharType*)_stream.ReadPointer();
        name._end = ReadToStringEnd<CharType>(_stream, _protectedStringMode, GetLocation());
        EatWhitespace<CharType>(_stream);

        _primed = Blob::None;
        _protectedStringMode = false;

        if (PeekNext() == Blob::AttributeValue) {
            value._start = (const CharType*)_stream.ReadPointer();
            value._end = ReadToStringEnd<CharType>(_stream, _protectedStringMode, GetLocation());
            _protectedStringMode = false;
            _primed = Blob::None;
        } else {
            value._start = value._end = nullptr;
        }

        return true;
    }

    template<typename CharType>
        StreamLocation InputStreamFormatter<CharType>::GetLocation() const
    {
        StreamLocation result;
        result._charIndex = 1 + unsigned((size_t(_stream.ReadPointer()) - size_t(_lineStart)) / sizeof(CharType));
        result._lineIndex = 1 + _lineIndex;
        return result;
    }

    template<typename CharType>
        InputStreamFormatter<CharType>::InputStreamFormatter(const MemoryMappedInputStream& stream) 
        : _stream(stream)
    {
        _primed = Blob::None;
        _activeLineSpaces = 0;
        _parentBaseLine = -1;
        _baseLineStackPtr = 0;
        _lineIndex = 0;
        _lineStart = _stream.ReadPointer();
        _protectedStringMode = false;
        _tabWidth = TabWidth;
        _pendingHeader = true;
    }

    template<typename CharType>
        InputStreamFormatter<CharType>::~InputStreamFormatter()
    {}

    template class InputStreamFormatter<utf8>;
    template class InputStreamFormatter<ucs4>;
    template class InputStreamFormatter<ucs2>;

    const utf8 FormatterConstants<utf8>::EndLine[] = { (utf8)'\r', (utf8)'\n' };
    const ucs2 FormatterConstants<ucs2>::EndLine[] = { (ucs2)'\r', (ucs2)'\n' };
    const ucs4 FormatterConstants<ucs4>::EndLine[] = { (ucs4)'\r', (ucs4)'\n' };

    const utf8 FormatterConstants<utf8>::ProtectedNamePrefix[] = { (utf8)'<', (utf8)':', (utf8)'(' };
    const ucs2 FormatterConstants<ucs2>::ProtectedNamePrefix[] = { (ucs2)'<', (ucs2)':', (ucs2)'(' };
    const ucs4 FormatterConstants<ucs4>::ProtectedNamePrefix[] = { (ucs4)'<', (ucs4)':', (ucs4)'(' };

    const utf8 FormatterConstants<utf8>::ProtectedNamePostfix[] = { (utf8)')', (utf8)':', (utf8)'>' };
    const ucs2 FormatterConstants<ucs2>::ProtectedNamePostfix[] = { (ucs2)')', (ucs2)':', (ucs2)'>' };
    const ucs4 FormatterConstants<ucs4>::ProtectedNamePostfix[] = { (ucs4)')', (ucs4)':', (ucs4)'>' };

    const utf8 FormatterConstants<utf8>::CommentPrefix[] = { (utf8)'~', (utf8)'~' };
    const ucs2 FormatterConstants<ucs2>::CommentPrefix[] = { (ucs2)'~', (ucs2)'~' };
    const ucs4 FormatterConstants<ucs4>::CommentPrefix[] = { (ucs4)'~', (ucs4)'~' };

    const utf8 FormatterConstants<utf8>::HeaderPrefix[] = { (utf8)'~', (utf8)'~', (utf8)'!' };
    const ucs2 FormatterConstants<ucs2>::HeaderPrefix[] = { (ucs2)'~', (ucs2)'~', (ucs2)'!' };
    const ucs4 FormatterConstants<ucs4>::HeaderPrefix[] = { (ucs4)'~', (ucs4)'~', (ucs4)'!' };

    const utf8 FormatterConstants<utf8>::Tab = (utf8)'\t';
    const ucs2 FormatterConstants<ucs2>::Tab = (ucs2)'\t';
    const ucs4 FormatterConstants<ucs4>::Tab = (ucs4)'\t';

    const utf8 FormatterConstants<utf8>::ElementPrefix = (utf8)'~';
    const ucs2 FormatterConstants<ucs2>::ElementPrefix = (ucs2)'~';
    const ucs4 FormatterConstants<ucs4>::ElementPrefix = (ucs4)'~';
}
