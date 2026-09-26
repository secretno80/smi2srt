"""subConverter conversion engine.

Subtitle parsing/serialization is delegated to established libraries
(pysubs2 for SRT/ASS and SAMI reading, pycaption for SAMI writing); this
package only adds the subConverter-specific rules on top: credit removal,
language detection and naming, video matching, and ASS styling.
"""
