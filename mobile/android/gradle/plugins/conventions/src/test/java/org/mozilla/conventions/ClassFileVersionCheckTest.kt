/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import org.gradle.api.GradleException
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test
import org.junit.jupiter.api.assertDoesNotThrow
import org.junit.jupiter.api.assertThrows
import org.junit.jupiter.api.io.TempDir
import java.io.DataOutputStream
import java.io.File

class ClassFileVersionCheckTest {
    @TempDir
    lateinit var dir: File

    private fun writeClass(name: String, major: Int) {
        DataOutputStream(File(dir, name).outputStream()).use {
            it.writeInt(0xCAFEBABE.toInt())
            it.writeShort(0)
            it.writeShort(major)
        }
    }

    @Test
    fun `Java 17 class files pass`() {
        writeClass("A.class", 61)
        File(dir, "sub").mkdir()
        writeClass("sub/B.class", 55)
        assertDoesNotThrow { checkClassFileVersions(dir, 17) }
    }

    @Test
    fun `class file newer than Java 17 fails`() {
        writeClass("A.class", 61)
        writeClass("B.class", 62)
        val e = assertThrows<GradleException> { checkClassFileVersions(dir, 17) }
        assertTrue(e.message!!.contains("B.class: class file version 62"), e.message)
        assertTrue(!e.message!!.contains("A.class"), e.message)
    }

    @Test
    fun `file with a class extension but no class file header fails`() {
        File(dir, "A.class").writeText("not a class file at all")
        val e = assertThrows<GradleException> { checkClassFileVersions(dir, 17) }
        assertTrue(e.message!!.endsWith("A.class is not a class file"), e.message)
    }

    @Test
    fun `truncated class file fails`() {
        File(dir, "A.class").writeBytes(byteArrayOf(0xCA.toByte(), 0xFE.toByte()))
        val e = assertThrows<GradleException> { checkClassFileVersions(dir, 17) }
        assertTrue(e.message!!.endsWith("A.class is not a class file"), e.message)
    }
}
