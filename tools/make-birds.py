#!/usr/bin/env python3
"""Tori shogi's birds as black silhouettes (100×100, y down), for the piece sets without kanji (traced into
data/fonts/ShogiBirds.otf by make-fonts.sh):
    make-birds.py <out dir>   writes SW GO FA EA CR PH LQ RQ PX .svg"""
import os, sys

EYE = '<circle cx="{}" cy="{}" r="2" fill="#fff"/>'
BIRDS = {
    # Swallow, from above: wings swept up and back, long forked tail.
    "SW": '<path d="M50 8C56 8 59 13 59 20C59 24 58 27 58 30C70 22 86 20 98 24C84 30 70 40 60 50C59 56 58 60 56 64'
          'L70 96C62 90 55 82 50 74C45 82 38 90 30 96L44 64C42 60 41 56 40 50C30 40 16 30 2 24C14 20 30 22 42 30'
          'C42 27 41 24 41 20C41 13 44 8 50 8Z"/>',
    # Goose, standing: long neck, heavy body.
    "GO": '<path d="M12 24L25 18C27 12 35 11 38 16C41 21 39 27 37 33C35 41 36 46 40 50C50 44 70 44 84 50L96 46'
          'C94 56 88 64 80 68C70 74 50 76 40 72C30 68 26 58 28 48C29 40 30 33 29 28Z"/>'
          '<path d="M50 72h4l1 16 7 4H48l3-4zM62 72h4l1 16 7 4H60l3-4z"/>' + EYE.format(32, 18),
    # Falcon, a head in profile as the chess knight is a horse's: hooked beak, a pale throat before
    # the dark "moustache" under the eye.
    "FA": '<path d="M27 29C34 21 44 16 56 16C70 16 80 23 83 35C85 46 82 60 78 80L84 94H30C32 84 37 75 42 68'
          'C38 64 35 60 33 56L30 57C27 55 27 52 29 50C24 51 18 53 14 58C13 50 15 42 20 36C22 33 24 31 27 29Z"/>'
          '<circle cx="45" cy="33" r="5.5" fill="#fff"/><circle cx="44" cy="33" r="2.5"/>'
          '<path d="M52 44C56 54 56 66 50 76C46 80 42 80 41 75C43 71 44 69 46 66C42 62 39 58 37 55C44 54 49 50 52 44Z"'
          ' fill="#fff"/>'
          '<path d="M64 76C70 70 74 62 76 52" fill="none" stroke="#fff" stroke-width="3" stroke-linecap="round"/>',
    # Eagle, heraldic: wings spread, head turned.
    "EA": '<path d="M42 36C32 28 18 18 5 9L10 22L3 24L12 32L4 37L15 42L8 48L22 51C30 53 38 53 42 52Z"/>'
          '<path d="M58 36C68 28 82 18 95 9L90 22L97 24L88 32L96 37L85 42L92 48L78 51C70 53 62 53 58 52Z"/>'
          '<path d="M42 34C42 28 46 26 50 26C54 26 58 28 58 34L60 60C60 66 58 70 56 72L65 93L50 86L35 93L44 72'
          'C42 70 40 66 40 60Z"/><circle cx="50" cy="20" r="8.5"/><path d="M43 17L33 23L43 26Z"/>'
          + EYE.format(48, 18),
    # Crane, standing: long neck and beak, long legs, drooping tail.
    "CR": '<path d="M10 18L30 13C32 8 39 8 41 13C43 17 41 23 42 29C43 36 46 41 52 41C62 37 76 39 85 47'
          'C91 53 93 62 89 69C85 65 81 63 77 63C71 67 63 67 57 65C49 63 43 57 41 49C38 41 36 31 35 23Z"/>'
          '<path d="M54 63h4v29l7 3H51l3-3zM62 64h3l6 14-8 13-2-2 7-11z"/>' + EYE.format(36, 14),
    # Pheasant, walking: ear tufts, long straight tail.
    "PH": '<path d="M8 35L17 30C17 23 23 19 30 21C35 23 35 30 33 36C41 40 50 42 58 46L98 20L96 28L63 56'
          'C61 64 55 70 45 72C33 72 25 64 25 54C25 46 27 42 27 39L17 37Z"/>'
          '<path d="M23 23L20 12L29 21ZM37 70h4l-3 19 5 3H32l3-3zM48 69h4l2 20 5 3H46l3-3z"/>' + EYE.format(26, 27),
    # Quail, the right one facing left (the left one, mirrored, right): round body, a plume curling forward.
    "RQ": '<path d="M14 52L24 46C24 37 31 31 40 31C45 31 49 34 51 38C63 36 79 42 87 54C91 62 89 70 83 76'
          'C73 84 55 86 43 82C31 78 25 70 27 60Z"/>'
          '<path d="M39 32C37 22 30 15 20 15C26 20 31 25 33 33Z"/>'
          '<path d="M44 81h4l-2 10 5 3H38l3-3zM58 83h4l1 8 5 3H54l3-3z"/>'
          '<path d="M52 56C62 52 74 56 80 66" fill="none" stroke="#fff" stroke-width="3" stroke-linecap="round"/>'
          + EYE.format(35, 43),
    # Phoenix: crest, raised wing, long curling tail plumes.
    "PX": '<path d="M10 28L20 23C21 16 27 12 33 14C38 16 39 22 37 29C38 37 42 42 50 44C58 46 63 52 61 58'
          'C57 65 48 65 42 61C34 57 30 47 30 37Z"/>'
          '<path d="M44 44C46 30 55 15 74 4L69 16L83 11L73 24L88 24L74 34L83 39C72 43 61 47 53 51Z"/>'
          '<g fill="none" stroke="#000" stroke-width="5" stroke-linecap="round">'
          '<path d="M31 15L37 4M35 16L47 8"/><path d="M58 58C71 66 73 81 64 89C59 93 65 97 72 93"/>'
          '<path d="M53 62C58 76 53 88 41 93"/><path d="M62 53C80 55 95 68 93 84C91 91 84 89 86 84"/></g>'
          + EYE.format(29, 20),
}
BIRDS["LQ"] = '<g transform="matrix(-1 0 0 1 100 0)">' + BIRDS["RQ"] + '</g>'

os.makedirs(sys.argv[1], exist_ok=True)
for code, body in BIRDS.items():
    with open(os.path.join(sys.argv[1], code + ".svg"), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">{body}</svg>\n')
